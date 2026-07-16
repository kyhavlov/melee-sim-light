#include "runtime/observation.h"

#include "gr/types.h"
#include "runtime/scalar.h"

#include <stddef.h>
#include <string.h>

enum {
    MSL_CORE_STAGE_FOUNTAIN_OF_DREAMS = 2,
    MSL_CORE_STAGE_YOSHIS_STORY = 8,
};

static void put_u16(uint8_t* out, size_t offset, uint16_t value)
{
    msl_core_put_le16(out + offset, value);
}

static void put_u32(uint8_t* out, size_t offset, uint32_t value)
{
    msl_core_put_le32(out + offset, value);
}

static void put_f32(uint8_t* out, size_t offset, float value)
{
    msl_core_put_lef32(out + offset, value);
}

static uint16_t player_u16(const MslCoreCompare* compare, size_t field,
                           int player)
{
    return msl_core_get_le16((const uint8_t*) compare + field +
                             (size_t) player * sizeof(uint16_t));
}

static float player_f32(const MslCoreCompare* compare, size_t field,
                        int player)
{
    return msl_core_get_lef32((const uint8_t*) compare + field +
                              (size_t) player * sizeof(float));
}

static void write_player(const MslCoreCompare* compare, int player,
                         uint8_t relation,
                         MslCoreObservationPlayer* output)
{
    const uint8_t* source = (const uint8_t*) compare;
    uint8_t* out = (uint8_t*) output;
    uint8_t hurtbox = source[offsetof(MslCoreCompare, hurtbox_state) + player];

    memset(output, 0, sizeof(*output));
    out[offsetof(MslCoreObservationPlayer, present)] = 1;
    out[offsetof(MslCoreObservationPlayer, source_player)] = (uint8_t) player;
    out[offsetof(MslCoreObservationPlayer, team_relation)] = relation;
    out[offsetof(MslCoreObservationPlayer, team_id)] =
        source[offsetof(MslCoreCompare, team_id) + player];
    put_f32(out, offsetof(MslCoreObservationPlayer, pos_x),
            player_f32(compare, offsetof(MslCoreCompare, pos_x), player));
    put_f32(out, offsetof(MslCoreObservationPlayer, pos_y),
            player_f32(compare, offsetof(MslCoreCompare, pos_y), player));
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_air_x_self),
            player_f32(compare, offsetof(MslCoreCompare, speed_air_x_self),
                       player));
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_ground_x_self),
            player_f32(compare,
                       offsetof(MslCoreCompare, speed_ground_x_self), player));
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_y_self),
            player_f32(compare, offsetof(MslCoreCompare, speed_y_self),
                       player));
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_x_attack),
            player_f32(compare, offsetof(MslCoreCompare, speed_x_attack),
                       player));
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_y_attack),
            player_f32(compare, offsetof(MslCoreCompare, speed_y_attack),
                       player));
    put_f32(out, offsetof(MslCoreObservationPlayer, percent),
            player_f32(compare, offsetof(MslCoreCompare, percent), player));
    put_f32(out, offsetof(MslCoreObservationPlayer, shield_hp),
            player_f32(compare, offsetof(MslCoreCompare, shield_hp), player));
    put_u16(out, offsetof(MslCoreObservationPlayer, action_id),
            player_u16(compare, offsetof(MslCoreCompare, action_id), player));
    put_u16(out, offsetof(MslCoreObservationPlayer, action_frame),
            player_u16(compare, offsetof(MslCoreCompare, action_frame),
                       player));
    put_u16(out, offsetof(MslCoreObservationPlayer, hitlag),
            player_u16(compare, offsetof(MslCoreCompare, hitlag), player));
    put_u16(out, offsetof(MslCoreObservationPlayer, hitstun),
            player_u16(compare, offsetof(MslCoreCompare, hitstun), player));
    out[offsetof(MslCoreObservationPlayer, char_id)] =
        source[offsetof(MslCoreCompare, char_id) + player];
    out[offsetof(MslCoreObservationPlayer, stocks)] =
        source[offsetof(MslCoreCompare, stocks) + player];
    out[offsetof(MslCoreObservationPlayer, facing)] =
        source[offsetof(MslCoreCompare, facing) + player];
    out[offsetof(MslCoreObservationPlayer, on_ground)] =
        source[offsetof(MslCoreCompare, on_ground) + player];
    out[offsetof(MslCoreObservationPlayer, jumps_left)] =
        source[offsetof(MslCoreCompare, jumps_left) + player];
    out[offsetof(MslCoreObservationPlayer, hurtbox_state)] = hurtbox;
    out[offsetof(MslCoreObservationPlayer, invulnerable)] = hurtbox != 0;
}

static void write_stage(const MslCoreMatch* match,
                        MslCoreObservationStage* output)
{
    uint8_t* out = (uint8_t*) output;
    int i;

    if (match->config.stage_id == MSL_CORE_STAGE_FOUNTAIN_OF_DREAMS) {
        for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
            const Ground* ground = &match->stage_ground[i];
            int side;
            if (!match->stage_ground_used[i] || ground->map_id != 4) {
                continue;
            }
            // grIzumi actor xC8 is reversed from Slippi's 0=right, 1=left
            // observation order; xD0 is the live height collision consumes.
            // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
            side = 1 - ground->gv.izumi3.xC8;
            if (side == 0) {
                put_f32(out,
                        offsetof(MslCoreObservationStage, fod_platforms) +
                            offsetof(MslCoreObservationFodPlatforms, right),
                        ground->gv.izumi3.xD0);
            } else if (side == 1) {
                put_f32(out,
                        offsetof(MslCoreObservationStage, fod_platforms) +
                            offsetof(MslCoreObservationFodPlatforms, left),
                        ground->gv.izumi3.xD0);
            }
        }
    } else if (match->config.stage_id == MSL_CORE_STAGE_YOSHIS_STORY) {
        for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
            const Ground* ground = &match->stage_ground[i];
            if (!match->stage_ground_used[i] || ground->map_id != 2 ||
                ground->u.randall.jobj == NULL)
            {
                continue;
            }
            // Randall's source JObj matrix is the moving collision actor's
            // already-published position; observation must not advance it.
            // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,
            //   grStory_801E33E0}
            out[offsetof(MslCoreObservationStage, randall) +
                offsetof(MslCoreObservationRandall, exists)] = 1;
            put_f32(out,
                    offsetof(MslCoreObservationStage, randall) +
                        offsetof(MslCoreObservationRandall, x),
                    ground->u.randall.jobj->mtx[0][3]);
            put_f32(out,
                    offsetof(MslCoreObservationStage, randall) +
                        offsetof(MslCoreObservationRandall, y),
                    ground->u.randall.jobj->mtx[1][3]);
            break;
        }
    }
}

int msl_core_match_write_observation(const MslCoreMatch* match,
                                     uint8_t viewpoint_player,
                                     MslCoreObservation* output)
{
    const MslCoreCompare* compare;
    const uint8_t* source;
    uint8_t* out;
    uint8_t viewpoint_team;
    int player;
    int slot = 0;

    if (match == NULL || output == NULL ||
        viewpoint_player >= match->config.num_players)
    {
        return -1;
    }
    compare = msl_core_match_output(match);
    source = (const uint8_t*) compare;
    out = (uint8_t*) output;
    memset(output, 0, sizeof(*output));
    memcpy(out + offsetof(MslCoreObservation, frame_id),
           source + offsetof(MslCoreCompare, frame_id), sizeof(uint32_t));
    memcpy(out + offsetof(MslCoreObservation, frame_pre_random_seed),
           source + offsetof(MslCoreCompare, frame_pre_random_seed),
           sizeof(uint32_t));
    memcpy(out + offsetof(MslCoreObservation, stage_id),
           source + offsetof(MslCoreCompare, stage_id), sizeof(uint32_t));
    out[offsetof(MslCoreObservation, num_players)] = match->config.num_players;
    out[offsetof(MslCoreObservation, viewpoint_player)] = viewpoint_player;
    out[offsetof(MslCoreObservation, is_teams)] = match->config.is_teams != 0;
    write_stage(match, &output->stage);

    viewpoint_team = source[offsetof(MslCoreCompare, team_id) +
                            viewpoint_player];
    write_player(compare, viewpoint_player, 0, &output->slots[slot++]);
    if (match->config.is_teams) {
        for (player = 0; player < match->config.num_players; ++player) {
            if (player != viewpoint_player &&
                source[offsetof(MslCoreCompare, team_id) + player] ==
                    viewpoint_team)
            {
                write_player(compare, player, 1, &output->slots[slot++]);
            }
        }
    }
    for (player = 0; player < match->config.num_players; ++player) {
        if (player != viewpoint_player &&
            (!match->config.is_teams ||
             source[offsetof(MslCoreCompare, team_id) + player] !=
                 viewpoint_team))
        {
            write_player(compare, player, 2, &output->slots[slot++]);
        }
    }
    memcpy(out + offsetof(MslCoreObservation, items),
           source + offsetof(MslCoreCompare, items),
           sizeof(compare->items));
    return 0;
}

void msl_core_match_write_terminal(const MslCoreMatch* match,
                                   int32_t max_frame_id,
                                   MslCoreTerminal* output)
{
    const MslCoreCompare* compare = msl_core_match_output(match);
    const uint8_t* source = (const uint8_t*) compare;
    uint8_t* out = (uint8_t*) output;
    uint8_t alive_count = 0;
    uint8_t team_mask = 0;
    uint8_t stockout = 0;
    uint8_t alive_teams = 0;
    uint8_t max_reached;
    int player;
    int team;

    memset(output, 0, sizeof(*output));
    memcpy(out + offsetof(MslCoreTerminal, frame_id),
           source + offsetof(MslCoreCompare, frame_id), sizeof(uint32_t));
    memcpy(out + offsetof(MslCoreTerminal, stage_id),
           source + offsetof(MslCoreCompare, stage_id), sizeof(uint32_t));
    for (player = 0; player < match->config.num_players; ++player) {
        uint8_t stocks = source[offsetof(MslCoreCompare, stocks) + player];
        if (stocks == 0) {
            stockout = 1;
        } else {
            uint8_t team_id =
                source[offsetof(MslCoreCompare, team_id) + player];
            ++alive_count;
            team_mask |= (uint8_t) (1U << (team_id < 8 ? team_id : 7));
        }
    }
    for (team = 0; team < 8; ++team) {
        alive_teams += (team_mask & (uint8_t) (1U << team)) != 0;
    }
    max_reached = max_frame_id >= 0 && match->frame_id >= max_frame_id;
    out[offsetof(MslCoreTerminal, done)] = match->rules.ended || max_reached;
    out[offsetof(MslCoreTerminal, match_ended)] = match->rules.ended != 0;
    out[offsetof(MslCoreTerminal, stockout)] = stockout;
    out[offsetof(MslCoreTerminal, max_frame_reached)] = max_reached;
    out[offsetof(MslCoreTerminal, alive_count)] = alive_count;
    out[offsetof(MslCoreTerminal, alive_team_count)] = alive_teams;
    out[offsetof(MslCoreTerminal, team_alive_mask)] = team_mask;
}
