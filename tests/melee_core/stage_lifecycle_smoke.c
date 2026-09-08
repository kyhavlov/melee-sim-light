#include "runtime/context.h"
#include "runtime/savestate.h"
#include "runtime/scalar.h"
#include "ft/ftlib.h"
#include "gr/ground.h"

#include <baselib/fog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Ground* controller(MslCoreMatch* match)
{
    msl_core_bind_match(match);
    return Ground_801C2BA4(3)->user_data;
}

int main(int argc, char** argv)
{
    MslCoreGameData game_data;
    MslCoreMatch matches[3];
    MslCoreMatchConfig config = { 0 };
    MslCoreInput input = { 0 };
    MslCoreStageEvents events = { 0 };
    void* snapshot;
    size_t snapshot_size;
    size_t arena_used;
    size_t allocations;
    unsigned visited = 0;
    unsigned previous = 0;
    int i;
    int frame;

    if (argc != 2) return 2;
    config.stage_id = MSL_STAGE_FINAL_DESTINATION;
    config.frame_id = -123;
    config.initial_random_seed = 1;
    config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1;
    config.num_players = 2;
    config.stock_count = 4;
    config.players[0].char_id = config.players[1].char_id = 1;
    if (msl_core_game_data_init(&game_data, argv[1]) != 0) return 1;
    for (i = 0; i < 3; ++i) {
        if (msl_core_match_init(&matches[i], &game_data, &config, &input) != 0)
            return 1;
    }
    arena_used = matches[0].memory.used;
    allocations = matches[0].memory.allocation_count;
    snapshot_size = msl_core_match_save_size(&matches[0]);
    snapshot = malloc(snapshot_size);
    if (snapshot == NULL) return 1;

    // Cross the source Ready callback, every background phase, and map 7's
    // destruction/recreation. Checkpoint at each phase boundary so relocation
    // covers live animation, color commands, fog and the pending start queue.
    for (frame = 0; frame < 14500; ++frame) {
        Ground* ground = controller(&matches[0]);
        unsigned phase = ground->u.map.xC4_b2_25;
        uint32_t frame_seed = UINT32_C(0x13579BDF) + ((uint32_t) frame << 16);
        int actor_exists = Ground_801C2BA4(7) != NULL;
        visited |= 1U << phase;
        if (actor_exists != (phase < 14 || phase == 17) ||
            matches[0].source.stage.x12C == NULL ||
            matches[0].source.stage.param != &matches[0].source.ground.params)
            goto fail;
        if (matches[0].frame_id <= -40) {
            if (matches[0].source.stage.x6A4 == NULL || !ground->u.map.xC4_b0)
                goto fail;
        } else if (matches[0].source.stage.x6A4 != NULL || ground->u.map.xC4_b0) {
            goto fail;
        }
        if (phase != previous || matches[0].frame_id == -40) {
            if (msl_core_match_copy(&matches[1], &matches[0]) != 0 ||
                msl_core_match_save(&matches[0], snapshot, snapshot_size, NULL) != 0 ||
                msl_core_match_restore(&matches[2], snapshot, snapshot_size) != 0)
                goto fail;
            previous = phase;
        }
        for (i = 0; i < 3; ++i) {
            if (msl_core_match_step(&matches[i], &input, frame_seed, &events) != 0 ||
                !matches[i].memory.sealed || matches[i].memory.used != arena_used ||
                matches[i].memory.allocation_count != allocations)
                goto fail;
        }
        for (i = 1; i < 3; ++i) {
            Ground* reference = controller(&matches[0]);
            Ground* other = controller(&matches[i]);
            HSD_Fog* fog = GET_FOG(matches[0].source.stage.x12C);
            HSD_Fog* other_fog = GET_FOG(matches[i].source.stage.x12C);
            if (memcmp(&matches[0].output, &matches[i].output,
                       sizeof(matches[0].output)) != 0 ||
                matches[0].random.value != matches[i].random.value ||
                reference->u.map.xC4_b2_25 != other->u.map.xC4_b2_25 ||
                reference->u.map.xC8 != other->u.map.xC8 ||
                reference->u.map.x104 != other->u.map.x104 ||
                fog->color.r != other_fog->color.r ||
                fog->color.g != other_fog->color.g ||
                fog->color.b != other_fog->color.b)
                goto fail;
        }
    }
    if (visited != ((1U << 18) - 2)) goto fail;

    // The Slippi direction vote excludes death bones before deciding whether
    // a tie needs RNG. Vanilla counts the left and right fighters equally.
    config.stage_id = MSL_STAGE_DREAM_LAND_N64;
    if (msl_core_match_reset(&matches[0], &game_data, &config, &input) != 0 ||
        msl_core_match_step(&matches[0], &input, 1, &events) != 0)
        goto fail;
    ((Fighter*) matches[0].fighters[0]->user_data)->motion_id = 0;
    matches[0].random.value = 1;
    if (ftLib_800864A8(&(Vec3) { 0 }, NULL) != -1 ||
        matches[0].random.value == 1)
        goto fail;
    matches[0].config.whispy_dead_fighter_fix = 1;
    matches[0].random.value = 1;
    if (ftLib_800864A8(&(Vec3) { 0 }, NULL) != 1 ||
        matches[0].random.value != 1)
        goto fail;

    free(snapshot);
    for (i = 0; i < 3; ++i) msl_core_match_destroy(&matches[i]);
    msl_core_game_data_deinit(&game_data);
    return 0;

fail:
    fprintf(stderr, "stage lifecycle failed at frame %d, phases %x\n", frame, visited);
    return 1;
}
