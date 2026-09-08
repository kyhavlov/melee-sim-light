#include "runtime/batch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    STEP_FRAMES = 2000,
    RESET_SAMPLES = 8,
    SNAPSHOT_SAMPLES = 4
};

static double now_ms(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (double) time.tv_sec * 1000.0 + (double) time.tv_nsec / 1000000.0;
}

static void config_init(MslCoreMatchConfig* config)
{
    memset(config, 0, sizeof(*config));
    config->stage_id = 32;
    config->frame_id = -123;
    config->frame_pre_random_seed = 1;
    config->initial_random_seed = 1;
    config->match_damage_ratio = 1.0F;
    config->num_players = 2;
    config->stock_count = 4;
    config->online_fnmsubs_zero = 1;
    config->brawl_offscreen_damage = 1;
    config->freeze_dead_up_fall_physics = 1;
    config->whispy_dead_fighter_fix = 1;
    config->ucf_cardinals_1_0_enabled = 1;
    config->ucf_shield_sdi_enabled = 1;
    config->ucf_sdi_enabled = 1;
    config->ucf_shield_drop_084_enabled = 1;
    config->players[0].char_id = 1;
    config->players[1].char_id = 22;
}

static int run_steps(MslCoreBatch* batch, const MslCoreInput* input,
                     MslCoreState* state, MslCoreViewerState* viewer,
                     int projection, double* elapsed_ms)
{
    int frame;
    double start = now_ms();
    for (frame = 0; frame < STEP_FRAMES; ++frame) {
        if (msl_core_batch_step_matches(batch, input, sizeof(*input), NULL,
                                        0) != MSL_CORE_OK)
        {
            return -1;
        }
        if (projection == 1 &&
            msl_core_batch_write_state(batch, state, sizeof(*state), NULL,
                                       0) != MSL_CORE_OK)
        {
            return -1;
        }
        if (projection == 2 &&
            msl_core_batch_write_viewer(batch, viewer, sizeof(*viewer), NULL,
                                        0) != MSL_CORE_OK)
        {
            return -1;
        }
    }
    *elapsed_ms = now_ms() - start;
    return 0;
}

int main(int argc, char** argv)
{
    MslCoreGameData* game_data = NULL;
    MslCoreBatch* batch = NULL;
    MslCoreMatchConfig config;
    MslCoreInput input = { 0 };
    MslCoreState state;
    MslCoreViewerState viewer;
    void* snapshot = NULL;
    size_t snapshot_size = 0;
    double game_data_ms;
    double batch_create_ms;
    double first_construct_ms;
    double reset_ms = 0.0;
    double step_ms;
    double state_ms;
    double viewer_ms;
    double save_ms = 0.0;
    double restore_ms = 0.0;
    double start;
    int sample;
    int result = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA\n", argv[0]);
        return 2;
    }
    config_init(&config);
    start = now_ms();
    if (msl_core_game_data_create(argv[1], &game_data) != MSL_CORE_OK) {
        goto done;
    }
    game_data_ms = now_ms() - start;
    start = now_ms();
    if (msl_core_batch_create(game_data, 1, &batch) != MSL_CORE_OK) {
        goto done;
    }
    batch_create_ms = now_ms() - start;
    start = now_ms();
    if (msl_core_batch_reset_matches(batch, &config, sizeof(config), NULL,
                                     0) != MSL_CORE_OK)
    {
        goto done;
    }
    first_construct_ms = now_ms() - start;
    for (sample = 0; sample < RESET_SAMPLES; ++sample) {
        start = now_ms();
        if (msl_core_batch_reset_matches(batch, &config, sizeof(config), NULL,
                                         0) != MSL_CORE_OK)
        {
            goto done;
        }
        reset_ms += now_ms() - start;
    }
    reset_ms /= RESET_SAMPLES;

    if (run_steps(batch, &input, &state, &viewer, 0, &step_ms) != 0 ||
        msl_core_batch_reset_matches(batch, &config, sizeof(config), NULL,
                                     0) != MSL_CORE_OK ||
        run_steps(batch, &input, &state, &viewer, 1, &state_ms) != 0 ||
        msl_core_batch_reset_matches(batch, &config, sizeof(config), NULL,
                                     0) != MSL_CORE_OK ||
        run_steps(batch, &input, &state, &viewer, 2, &viewer_ms) != 0 ||
        msl_core_batch_match_save_size(batch, 0, &snapshot_size) !=
            MSL_CORE_OK ||
        (snapshot = malloc(snapshot_size)) == NULL)
    {
        goto done;
    }
    for (sample = 0; sample < SNAPSHOT_SAMPLES; ++sample) {
        start = now_ms();
        if (msl_core_batch_save_match(batch, 0, snapshot, snapshot_size,
                                      NULL) != MSL_CORE_OK)
        {
            goto done;
        }
        save_ms += now_ms() - start;
        start = now_ms();
        if (msl_core_batch_restore_match(batch, 0, snapshot, snapshot_size) !=
            MSL_CORE_OK)
        {
            goto done;
        }
        restore_ms += now_ms() - start;
    }
    save_ms /= SNAPSHOT_SAMPLES;
    restore_ms /= SNAPSHOT_SAMPLES;
    printf("{\"game_data_ms\":%.3f,\"batch_create_ms\":%.3f,"
           "\"first_match_construct_ms\":%.3f,\"reset_ms\":%.3f,"
           "\"step_compare_fps\":%.1f,\"step_state_fps\":%.1f,"
           "\"step_viewer_fps\":%.1f,\"save_ms\":%.3f,"
           "\"restore_ms\":%.3f,\"snapshot_bytes\":%zu}\n",
           game_data_ms, batch_create_ms, first_construct_ms, reset_ms,
           STEP_FRAMES * 1000.0 / step_ms, STEP_FRAMES * 1000.0 / state_ms,
           STEP_FRAMES * 1000.0 / viewer_ms, save_ms, restore_ms,
           snapshot_size);
    result = 0;

done:
    free(snapshot);
    msl_core_batch_destroy(batch);
    msl_core_game_data_destroy(game_data);
    return result;
}
