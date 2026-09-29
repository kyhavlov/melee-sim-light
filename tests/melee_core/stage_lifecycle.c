#include "runtime/context.h"
#include "runtime/observation.h"
#include "runtime/savestate.h"
#include "runtime/scalar.h"
#include "runtime/viewer.h"
#include "ft/ftlib.h"
#include "gr/ground.h"
#include "mp/mpisland.h"
#include "mp/mplib.h"

#include <baselib/fog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Ground* controller(MslCoreMatch* match)
{
    msl_core_bind_match(match);
    return Ground_801C2BA4(3)->user_data;
}

static int whispy_is(const MslCoreMatch* match, uint8_t direction)
{
    MslCoreObservation live;
    MslCoreObservation compared;
    memset(&live, 0xA5, sizeof(live));
    memset(&compared, 0xA5, sizeof(compared));
    return msl_core_match_write_observation(match, 0, &live) == 0 &&
           msl_core_match_write_observation_from_compare(match, 0, &compared) == 0 &&
           live.stage.whispy == direction &&
           memcmp(&live, &compared, sizeof(live)) == 0;
}

static int check_randall_surface(MslCoreMatch* match, const MslCoreGameData* data,
                                 MslCoreMatchConfig config)
{
    MslCoreInput input = { 0 };
    int frame;
    config.stage_id = MSL_STAGE_YOSHIS_STORY;
    if (msl_core_match_reset(match, data, &config, &input) != 0) return 1;
    for (frame = 0; frame < 1200; ++frame) {
        MslCoreObservation observation;
        MslCoreViewerState viewer;
        Vec3 hit;
        Vec3 normal;
        Vec3 left;
        Vec3 right;
        int line;
        u32 flags;
        float x;
        float y;
        if (msl_core_match_step(match, &input, (uint32_t) frame,
                                &(MslCoreStageEvents) { 0 }) != 0) return 1;
        msl_core_match_write_observation(match, 0, &observation);
        msl_core_match_write_viewer(match, &viewer);
        x = msl_core_get_lef32(&observation.stage.randall.x);
        y = msl_core_get_lef32(&observation.stage.randall.y);
        if (!observation.stage.randall.exists || !viewer.stage.randall_exists ||
            msl_core_get_lef32(&viewer.stage.randall_x) != x ||
            msl_core_get_lef32(&viewer.stage.randall_y) != y ||
            !mpCheckFloor(x, y + 1, x, y - 1, 0, &hit, &line, &flags,
                          &normal, -1, -1, -1, NULL, NULL))
        {
            fprintf(stderr, "Randall observation is not on a collision floor at %d\n", frame);
            return 1;
        }
        mpFloorGetLeft(line, &left);
        mpFloorGetRight(line, &right);
        if (x != (left.x + right.x) * 0.5F || y != (left.y + right.y) * 0.5F) {
            fprintf(stderr, "Randall surface differs from source collision endpoints\n");
            return 1;
        }
    }
    return 0;
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

    // Replay events change the source actor at the callback epilogue. The
    // observation must publish that frame's value, and retain it on frames
    // without a new event, just as libmelee retains its last 0x40 payload.
    config.stage_flags = MSL_STAGE_WHISPY_EVENTS;
    if (msl_core_match_reset(&matches[0], &game_data, &config, &input) != 0 ||
        !whispy_is(&matches[0], MSL_WHISPY_NONE))
        goto fail;
    free(snapshot);
    snapshot_size = msl_core_match_save_size(&matches[0]);
    snapshot = malloc(snapshot_size);
    if (snapshot == NULL) goto fail;
    {
        const uint8_t directions[] = {
            MSL_WHISPY_LEFT, MSL_WHISPY_RIGHT, MSL_WHISPY_NONE,
        };
        for (i = 0; i < 3; ++i) {
            events.dreamland_whispy_valid = 1;
            events.dreamland_whispy_direction = directions[i];
            if (msl_core_match_step(&matches[0], &input, 1, &events) != 0 ||
                !whispy_is(&matches[0], directions[i]))
                goto fail;
            events.dreamland_whispy_valid = 0;
            if (msl_core_match_step(&matches[0], &input, 1, &events) != 0 ||
                !whispy_is(&matches[0], directions[i]) ||
                msl_core_match_copy(&matches[1], &matches[0]) != 0 ||
                msl_core_match_save(&matches[0], snapshot, snapshot_size, NULL) != 0 ||
                msl_core_match_restore(&matches[2], snapshot, snapshot_size) != 0 ||
                !whispy_is(&matches[1], directions[i]) ||
                !whispy_is(&matches[2], directions[i]))
                goto fail;
        }
    }
    // A dirty observation must not carry a previous Dream Land wind into
    // another stage or a fresh match.
    events.dreamland_whispy_valid = 1;
    events.dreamland_whispy_direction = MSL_WHISPY_RIGHT;
    if (msl_core_match_step(&matches[0], &input, 1, &events) != 0 ||
        !whispy_is(&matches[0], MSL_WHISPY_RIGHT))
        goto fail;
    config.stage_flags = 0;
    config.stage_id = MSL_STAGE_FINAL_DESTINATION;
    if (msl_core_match_reset(&matches[0], &game_data, &config, &input) != 0 ||
        !whispy_is(&matches[0], MSL_WHISPY_NONE))
        goto fail;
    config.stage_id = MSL_STAGE_DREAM_LAND_N64;
    if (msl_core_match_reset(&matches[0], &game_data, &config, &input) != 0 ||
        !whispy_is(&matches[0], MSL_WHISPY_NONE))
        goto fail;

    if (check_randall_surface(&matches[0], &game_data, config) != 0)
        goto fail;

    config.stage_id = MSL_STAGE_POKEMON_STADIUM;
    events = (MslCoreStageEvents) { 0 };
    if (msl_core_match_reset(&matches[0], &game_data, &config, &input) != 0)
        goto fail;
    {
        const struct mpIsland_80458E88_t* islands =
            &matches[0].source.mp_lib.island_root;
        int first_floor = islands->next->x24;
        int last_static_floor = islands->x8->x24;
        for (i = 0; i < 2; ++i) {
            if (msl_core_match_step(&matches[0], &input, 1, &events) != 0)
                goto fail;
        }
        msl_core_bind_match(&matches[0]);
        if (first_floor == last_static_floor ||
            msl_mp_floor_islands()->x24 != first_floor ||
            msl_core_match_step(&matches[0], &input, 1, &events) != 0)
            goto fail;

        // Copy and restore before the first read of the reversed order.
        free(snapshot);
        snapshot_size = msl_core_match_save_size(&matches[0]);
        snapshot = malloc(snapshot_size);
        if (snapshot == NULL ||
            msl_core_match_copy(&matches[1], &matches[0]) != 0 ||
            msl_core_match_save(&matches[0], snapshot, snapshot_size, NULL) != 0 ||
            msl_core_match_restore(&matches[2], snapshot, snapshot_size) != 0)
            goto fail;
        for (i = 0; i < 3; ++i) {
            msl_core_bind_match(&matches[i]);
            if (msl_mp_floor_islands()->x24 != last_static_floor)
                goto fail;
        }
    }

    free(snapshot);
    for (i = 0; i < 3; ++i) msl_core_match_destroy(&matches[i]);
    msl_core_game_data_deinit(&game_data);
    return 0;

fail:
    fprintf(stderr, "stage lifecycle failed at frame %d, phases %x\n", frame, visited);
    return 1;
}
