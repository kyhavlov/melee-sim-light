#include "api.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint64_t digest(const void* data, size_t size)
{
    const uint8_t* bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

int main(int argc, char** argv)
{
    MslCoreGameData* game_data = NULL;
    MslCoreBatch* batch = NULL;
    MslCoreMatchConfig config = { 0 };
    MslCoreInput input = { 0 };
    MslCoreState state;
    int frame;
    int result = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA\n", argv[0]);
        return 2;
    }
    config.stage_id = 32;
    config.frame_id = -123;
    config.frame_pre_random_seed = 1;
    config.initial_random_seed = 1;
    config.match_damage_ratio = 1.0F;
    config.num_players = 2;
    config.stock_count = 4;
    config.players[0].char_id = 1;
    config.players[1].char_id = 1;
    input.p[0].main_x = 80;

    if (msl_core_game_data_create(argv[1], &game_data) != MSL_CORE_OK ||
        msl_core_batch_create(game_data, 1, &batch) != MSL_CORE_OK ||
        msl_core_batch_reset_matches(batch, &config, sizeof(config), NULL,
                                     0) != MSL_CORE_OK)
    {
        goto done;
    }
    for (frame = 0; frame < 90; ++frame) {
        if (msl_core_batch_step_matches(batch, &input, sizeof(input), NULL,
                                        0) != MSL_CORE_OK)
        {
            goto done;
        }
    }
    if (msl_core_batch_write_state(batch, &state, sizeof(state), NULL, 0) !=
        MSL_CORE_OK)
    {
        goto done;
    }
    printf("%016" PRIx64 "\n", digest(&state, sizeof(state)));
    result = 0;

done:
    msl_core_batch_destroy(batch);
    msl_core_game_data_destroy(game_data);
    return result;
}
