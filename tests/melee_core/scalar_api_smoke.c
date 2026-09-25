#include "runtime/scalar.h"
#include "runtime/observation.h"
#include "runtime/viewer.h"
#include "mp/mplib.h"
#include "ft/fighter.h"
#include "ft/chara/ftCommon/forward.h"
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

static int check_absent_observation(MslCoreMatch* match)
{
    Fighter* fp = GET_FIGHTER(match->fighters[0]);
    Fighter saved = *fp;
    int stocks = Player_GetStocks(fp->player_id);
    MslCoreObservation direct;
    MslCoreObservation compared;
    MslCoreObservationPlayer expected = { 0 };
    int result = 0;

    Player_SetStocks(fp->player_id, 0);
    fp->motion_id = ftCo_MS_DeadDown;
    fp->x221F_b3 = 0;
    fp->dmg.x1830_percent = 43;
    msl_core_match_write_observation(match, 0, &direct);
    if (!direct.slots[0].present || direct.slots[0].percent != 43) {
        fprintf(stderr, "KO animation disappeared before Sleep\n");
        result = 1;
    }
    fp->motion_id = ftCo_MS_Sleep;
    fp->x221F_b3 = 1;
    msl_core_match_write_observation(match, 0, &direct);
    msl_core_match_write_observation_from_compare(match, 0, &compared);
    expected.char_id = fp->kind;
    expected.team_id = fp->team;
    if (memcmp(&direct.slots[0], &expected, sizeof(expected)) != 0 ||
        memcmp(&direct, &compared, sizeof(direct)) != 0 ||
        fp->dmg.x1830_percent != 43 || fp->motion_id != ftCo_MS_Sleep)
    {
        fprintf(stderr, "absent fighter observation leaked state or changed the fighter\n");
        result = 1;
    }
    *fp = saved;
    Player_SetStocks(fp->player_id, stocks);
    return result;
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
        x = observation.stage.randall.x;
        y = observation.stage.randall.y;
        if (!observation.stage.randall.exists || !viewer.stage.randall_exists ||
            viewer.stage.randall_x != x || viewer.stage.randall_y != y ||
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
    MslCoreMatchConfig config;
    MslCoreInput previous_input;
    MslCoreInput input;
    const MslCoreCompare* output;
    MslCoreCompare first_row;
    MslCoreMatch match;
    int i;

    if (argc < 2 || argc > 5) {
        fprintf(stderr, "usage: %s GAME_DATA [STAGE_ID [CHAR_ID [OPPONENT_ID]]]\n", argv[0]);
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
    config.players[1].char_id =
        argc >= 5 ? (uint8_t) strtoul(argv[4], NULL, 0)
                  : config.players[0].char_id;

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
    for (i = 0; i < FTKIND_MAX; ++i) {
        const void* copy = i == 0
                               ? (const void*) game_data.source.fighter.kirby_copy.x0
                               : (const void*) game_data.source.fighter.kirby_copy.hats[i - 1];
        if (copy != NULL && msl_memory_context_owns(&match.memory, copy)) {
            fprintf(stderr, "Kirby copy %d is cached in the Match arena\n", i);
            return 1;
        }
    }
    for (i = 0; i < 5 * 12; ++i) {
        const void* model = game_data.source.fighter.kirby_costume_hats[i / 12][i % 12];
        if (model != NULL && msl_memory_context_owns(&match.memory, model)) {
            fprintf(stderr, "Kirby costume model %d is cached in the Match arena\n", i);
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
    if (check_absent_observation(&match) != 0) return 1;
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
    if (check_randall_surface(&match, &game_data, config) != 0) return 1;
    msl_core_match_destroy(&match);
    return 0;
}
