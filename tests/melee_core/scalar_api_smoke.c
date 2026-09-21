#include "runtime/scalar.h"
#include "runtime/wire.h"
#include "runtime/context.h"
#include "runtime/match.h"
#include "gm/gm_1601.h"
#include "pl/player.h"
#include "ft/types.h"
#include "platform/memory.h"

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
    MslCoreCompare first_row;
    MslCoreMatch match;
    int i;

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
        msl_core_match_step(&match, &input, config.frame_pre_random_seed,
                            &(MslCoreStageEvents) { 0 }) !=
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
    first_row = *output;

    // Cached costume models must survive recycling the Match arena.
    for (i = 0; i < 2; ++i) {
        Fighter* fp = (Fighter*) match.fighters[i]->user_data;
        if (msl_memory_context_owns(&match.memory, fp->x108_costume_joint)) {
            fprintf(stderr, "fighter %d caches its costume model in the Match arena\n", i);
            return 1;
        }
    }

    // A reset with the same configuration must construct the same match:
    // its first output row equals the fresh construction's.
    if (msl_core_match_reset(&match, &game_data, &config, &previous_input) !=
            0 ||
        msl_core_match_step(&match, &input, config.frame_pre_random_seed,
                            &(MslCoreStageEvents) { 0 }) != 0) {
        fprintf(stderr, "same-configuration reset failed to construct\n");
        return 1;
    }
    if (memcmp(msl_core_match_output(&match), &first_row, sizeof(first_row)) !=
        0) {
        fprintf(stderr, "same-configuration reset diverged from the fresh "
                        "construction's first row\n");
        return 1;
    }
    msl_core_bind_match(&match);
    msl_core_bind_match_rules(&match.rules);

    Player_SetStocks(match.source_slots[0], 0);
    gm_80167320(match.source_slots[0], false);
    if (!match.rules.ended) {
        fprintf(stderr, "singles last stock did not end the match\n");
        return 1;
    }
    config.num_players = 4;
    config.is_teams = 1;
    config.players[2].char_id = config.players[0].char_id;
    config.players[3].char_id = config.players[0].char_id;
    config.players[1].team_id = config.players[2].team_id = 1;
    if (msl_core_match_reset(&match, &game_data, &config, &previous_input) != 0) {
        return 1;
    }
    msl_core_bind_match(&match);
    msl_core_bind_match_rules(&match.rules);
    Player_SetStocks(match.source_slots[1], 0);
    gm_80167320(match.source_slots[1], false);
    if (match.rules.ended) {
        fprintf(stderr, "doubles ended with a living teammate\n");
        return 1;
    }
    Player_SetStocks(match.source_slots[2], 0);
    gm_80167320(match.source_slots[2], false);
    if (!match.rules.ended) {
        fprintf(stderr, "doubles team elimination did not end the match\n");
        return 1;
    }
    msl_core_match_destroy(&match);
    return 0;
}
