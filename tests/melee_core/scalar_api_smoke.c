#include "runtime/scalar.h"
#include "runtime/wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    MslCoreGameData game_data;
    MslCoreMatchConfig config;
    MslCoreInput previous_input;
    MslCoreInput input;
    const MslCoreCompare* output;
    MslCoreMatch match;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s GAME_DATA [STAGE_ID [CHAR_ID]]\n", argv[0]);
        return 2;
    }
    memset(&config, 0, sizeof(config));
    memset(&previous_input, 0, sizeof(previous_input));
    memset(&input, 0, sizeof(input));
    config.stage_id = argc >= 3 ? (uint8_t) strtoul(argv[2], NULL, 0) : 32;
    config.frame_id = -123;
    config.frame_pre_random_seed = 1;
    config.initial_random_seed = 1;
    config.match_damage_ratio = 1.0F;
    config.num_players = 2;
    config.stock_count = 4;
    config.players[0].char_id =
        argc >= 4 ? (uint8_t) strtoul(argv[3], NULL, 0) : 1;
    config.players[1].char_id = config.players[0].char_id;

    if (msl_core_game_data_init(&game_data, argv[1]) != 0 ||
        msl_core_match_init(&match, &game_data, &config, &previous_input) !=
            0 ||
        msl_core_match_step(&match, &input, config.frame_pre_random_seed) !=
            0) {
        return 1;
    }
    output = msl_core_match_output(&match);
    if ((int32_t) msl_core_get_le32(&output->frame_id) != -122 ||
        msl_core_get_le32(&output->stage_id) != config.stage_id ||
        output->num_players != 2) {
        fprintf(stderr, "private scalar API produced an invalid first row\n");
        return 1;
    }
    return 0;
}
