#include "runtime/context.h"
#include "runtime/savestate.h"
#include "runtime/scalar.h"
#include "runtime/viewer.h"
#include "gr/ground.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Ground* controller(MslCoreMatch* match)
{
    msl_core_bind_match(match);
    return Ground_801C2BA4(2)->user_data;
}

int main(int argc, char** argv)
{
    MslCoreGameData data;
    MslCoreMatch matches[3];
    MslCoreMatchConfig config = { 0 };
    MslCoreInput input = { 0 };
    MslCoreStageEvents events = { 0 };
    MslCoreViewerState views[2];
    size_t arena_used, frozen_used, allocations, snapshot_size;
    void* snapshot;
    unsigned types = 0, phases = 0;
    int previous = -1, frame, i;
    if (argc != 2) return 2;
    config.stage_id = MSL_STAGE_POKEMON_STADIUM;
    config.frame_id = -123;
    config.initial_random_seed = config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1;
    config.num_players = 2;
    config.stock_count = 4;
    config.stadium_transformations = 1;
    config.players[0].char_id = config.players[1].char_id = 1;
    if (msl_core_game_data_init(&data, argv[1])) return 1;
    for (i = 0; i < 3; ++i)
        if (msl_core_match_init(&matches[i], &data, &config, &input)) return 1;
    arena_used = matches[0].memory.used;
    allocations = matches[0].memory.allocation_count;
    snapshot_size = msl_core_match_save_size(&matches[0]);
    snapshot = malloc(snapshot_size);
    if (!snapshot) return 1;
    for (frame = 0; frame < 80000; ++frame) {
        Ground* ground = controller(&matches[0]);
        int phase = ground->u.stadium.xDC;
        uint32_t frame_seed = matches[0].random_seed;
        phases |= 1U << phase;
        types |= 1U << ground->u.stadium.xDE;
        if (phase != previous) {
            if (msl_core_match_copy(&matches[1], &matches[0]) ||
                msl_core_match_save(&matches[0], snapshot, snapshot_size, NULL) ||
                msl_core_match_restore(&matches[2], snapshot, snapshot_size)) goto fail;
            previous = phase;
        }
        for (i = 0; i < 3; ++i) {
            if (msl_core_match_step(&matches[i], &input, frame_seed, &events) ||
                matches[i].memory.used != arena_used ||
                matches[i].memory.allocation_count != allocations) goto fail;
        }
        for (i = 1; i < 3; ++i) {
            Ground* reference = controller(&matches[0]);
            Ground* other = controller(&matches[i]);
            if (reference->u.stadium.xDC != other->u.stadium.xDC ||
                reference->u.stadium.xDE != other->u.stadium.xDE ||
                reference->u.stadium.xD8 != other->u.stadium.xD8 ||
                matches[0].random_seed != matches[i].random_seed) goto fail;
            if (frame % 31 == 0) {
                msl_core_match_write_viewer(&matches[0], &views[0]);
                msl_core_match_write_viewer(&matches[i], &views[1]);
                if (memcmp(views, views + 1, sizeof(views[0])) ||
                    !views[0].stage.collision_line_count) goto fail;
            }
        }
    }
    if (types != ((1U << 3) | (1U << 4) | (1U << 5) | (1U << 6) | (1U << 9)) ||
        phases != 0x7F) goto fail;
    config.stadium_transformations = 0;
    if (msl_core_match_reset(&matches[0], &data, &config, &input)) goto fail;
    for (frame = 0; frame < 8000; ++frame)
        if (msl_core_match_step(&matches[0], &input, matches[0].random_seed, &events)) goto fail;
    if (controller(&matches[0])->u.stadium.xDE != 5 ||
        controller(&matches[0])->u.stadium.xDC != 0) goto fail;
    frozen_used = matches[0].memory.used;
    config.stadium_transformations = 1;
    if (msl_core_match_reset(&matches[0], &data, &config, &input) ||
        matches[0].memory.used != arena_used) goto fail;
    printf("Stadium: all transformations, sealed pools, copy/save/restore and resets passed (enabled=%zu frozen=%zu arena bytes)\n", arena_used, frozen_used);
    free(snapshot);
    for (i = 0; i < 3; ++i) msl_core_match_destroy(&matches[i]);
    msl_core_game_data_deinit(&data);
    return 0;
fail:
    fprintf(stderr, "Stadium lifecycle failed at frame %d, types=%x phases=%x\n", frame, types, phases);
    return 1;
}
