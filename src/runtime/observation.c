#include "runtime/observation.h"

#include "ft/chara/ftCommon/forward.h"
#include "ft/fighter.h"
#include "ft/types.h"
#include "gr/types.h"
#include "mp/types.h"
#include "runtime/item_projection.h"
#include "runtime/scalar.h"

#include <math.h>
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

static uint16_t float_frames_u16(float value)
{
    if (value <= 0.0F) {
        return 0;
    }
    if (value >= 65535.0F) {
        return 65535;
    }
    return (uint16_t) floorf(value);
}

static int16_t state_age_i16(float value)
{
    if (value <= -32768.0F) {
        return -32768;
    }
    if (value >= 32767.0F) {
        return 32767;
    }
    return (int16_t) floorf(value);
}

static uint16_t compare_player_u16(const MslCoreCompare* compare,
                                   size_t field, int player)
{
    return msl_core_get_le16((const uint8_t*) compare + field +
                             (size_t) player * sizeof(uint16_t));
}

static float compare_player_f32(const MslCoreCompare* compare, size_t field,
                                int player)
{
    return msl_core_get_lef32((const uint8_t*) compare + field +
                              (size_t) player * sizeof(float));
}

static void write_player_from_compare(const MslCoreCompare* compare,
                                      int player, uint8_t relation,
                                      MslCoreObservationPlayer* output)
{
    const uint8_t* source = (const uint8_t*) compare;
    uint8_t* out = (uint8_t*) output;
    uint8_t hurtbox =
        source[offsetof(MslCoreCompare, hurtbox_state) + player];

    out[offsetof(MslCoreObservationPlayer, source_player)] = (uint8_t) player;
    out[offsetof(MslCoreObservationPlayer, team_relation)] = relation;
    out[offsetof(MslCoreObservationPlayer, team_id)] =
        source[offsetof(MslCoreCompare, team_id) + player];
    out[offsetof(MslCoreObservationPlayer, char_id)] =
        source[offsetof(MslCoreCompare, char_id) + player];
    out[offsetof(MslCoreObservationPlayer, stocks)] =
        source[offsetof(MslCoreCompare, stocks) + player];
    if (compare_player_u16(compare, offsetof(MslCoreCompare, action_id), player) ==
        ftCo_MS_Sleep)
    {
        return;
    }
    out[offsetof(MslCoreObservationPlayer, present)] = 1;
#define COPY_F32(field)                                                       \
    put_f32(out, offsetof(MslCoreObservationPlayer, field),                  \
            compare_player_f32(compare, offsetof(MslCoreCompare, field),     \
                               player))
#define COPY_U16(field)                                                       \
    put_u16(out, offsetof(MslCoreObservationPlayer, field),                  \
            compare_player_u16(compare, offsetof(MslCoreCompare, field),     \
                               player))
    COPY_F32(pos_x);
    COPY_F32(pos_y);
    COPY_F32(speed_air_x_self);
    COPY_F32(speed_ground_x_self);
    COPY_F32(speed_y_self);
    COPY_F32(speed_x_attack);
    COPY_F32(speed_y_attack);
    COPY_F32(percent);
    COPY_F32(shield_hp);
    COPY_U16(action_id);
    COPY_U16(action_frame);
    COPY_U16(hitlag);
    COPY_U16(hitstun);
#undef COPY_F32
#undef COPY_U16
    out[offsetof(MslCoreObservationPlayer, facing)] =
        source[offsetof(MslCoreCompare, facing) + player];
    out[offsetof(MslCoreObservationPlayer, on_ground)] =
        source[offsetof(MslCoreCompare, on_ground) + player];
    out[offsetof(MslCoreObservationPlayer, jumps_left)] =
        source[offsetof(MslCoreCompare, jumps_left) + player];
    out[offsetof(MslCoreObservationPlayer, hurtbox_state)] = hurtbox;
    out[offsetof(MslCoreObservationPlayer, invulnerable)] = hurtbox != 0;
}

// Slippi records the low byte of each charge and the raw 32-bit character
// value. Read the same fighter lanes for policy observations and validation.
// refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm::STORED_STATE_TABLE_BLRL
static inline void msl_core_write_stored_fighter(const Fighter* fp,
                                                MslObservationStored* out)
{
    uint8_t spent = (fp->used_tether ? MSL_SPENT_TETHER : 0) |
                    (fp->x2224_b1 ? MSL_SPENT_AIR_ITEM_CATCH : 0);

    out->wall_jumps = fp->x1969_walljumpUsed;
    switch (fp->kind) {
    case FTKIND_MARIO:
    case FTKIND_DRMARIO:
        spent |= fp->fv.mr.x2238_isCapeBoost ? MSL_SPENT_SIDE_LIFT : 0;
        spent |= fp->fv.mr.x2234_tornadoCharge ? MSL_SPENT_DOWN_LIFT : 0;
        break;
    case FTKIND_DONKEY:
        out->charge = fp->fv.dk.x222C;
        break;
    case FTKIND_SEAK:
        out->charge = fp->fv.sk.x0;
        break;
    case FTKIND_SAMUS:
        out->charge = fp->fv.ss.x2230;
        break;
    case FTKIND_MEWTWO:
        out->charge = fp->fv.mt.x2234_shadowBallCharge;
        spent |= fp->fv.mt.x223C_isConfusionBoost ? MSL_SPENT_SIDE_LIFT : 0;
        break;
    case FTKIND_LUIGI:
        spent |= fp->fv.lg.x222C_cycloneCharge ? MSL_SPENT_DOWN_LIFT : 0;
        break;
    case FTKIND_MARS:
    case FTKIND_EMBLEM:
        spent |= fp->fv.ms.x222C ? MSL_SPENT_SIDE_LIFT : 0;
        break;
    case FTKIND_PEACH:
        spent |= fp->fv.pe.specialairn_used ? MSL_SPENT_NEUTRAL_LIFT : 0;
        spent |= fp->fv.pe.has_float ? 0 : MSL_SPENT_FLOAT;
        msl_core_put_lef32(&out->character_value, fp->fv.pe.x4);
        break;
    case FTKIND_POPO:
        spent |= fp->fv.pp.x224C ? MSL_SPENT_NEUTRAL_LIFT : 0;
        break;
    case FTKIND_NANA:
        spent |= fp->fv.nn.x224C ? MSL_SPENT_NEUTRAL_LIFT : 0;
        break;
    case FTKIND_KOOPA:
        msl_core_put_lef32(&out->character_value, fp->fv.kp.x222C);
        break;
    case FTKIND_GAMEWATCH:
        out->charge = fp->fv.gw.x2238_panicCharge;
        out->character_byte = fp->fv.gw.x222C_judgeVar1 |
                              (fp->fv.gw.x2230_judgeVar2 << 4);
        msl_core_put_le32(&out->oil_panic_damage, fp->fv.gw.x223C_panicDamage);
        break;
    case FTKIND_KIRBY:
        spent |= fp->fv.kb.x64 ? MSL_SPENT_SIDE_LIFT : 0;
        out->character_byte = fp->fv.kb.hat.kind;
        switch (fp->fv.kb.hat.kind) {
        case FTKIND_DONKEY:
            out->charge = fp->fv.kb.xBC;
            break;
        case FTKIND_SEAK:
            out->charge = fp->fv.kb.xB4;
            break;
        case FTKIND_SAMUS:
            out->charge = fp->fv.kb.xA8;
            break;
        case FTKIND_MEWTWO:
            out->charge = fp->fv.kb.x9C;
            break;
        case FTKIND_PEACH:
            spent |= fp->fv.kb.xCC ? MSL_SPENT_NEUTRAL_LIFT : 0;
            break;
        case FTKIND_POPO:
        case FTKIND_NANA:
            spent |= fp->fv.kb.xC4 ? MSL_SPENT_NEUTRAL_LIFT : 0;
            break;
        case FTKIND_KOOPA:
            msl_core_put_lef32(&out->character_value, fp->fv.kb.x84);
            break;
        }
        break;
    }
    out->spent = spent;
}

static void write_fighter(const MslCoreMatch* match, const Fighter* fp,
                          float pos_x, float pos_y, int player,
                          uint8_t relation, MslCoreObservationPlayer* output)
{
    uint8_t* out = (uint8_t*) output;
    float hitstun = fp->x221C_b6 ? fp->mv.co.damage.x0 : 0.0F;
    int hurtbox = fp->x1988 != 0 ? fp->x1988 : fp->x198C;
    int jumps_left = fp->co_attrs.max_jumps - fp->x1968_jumpsUsed;

    out[offsetof(MslCoreObservationPlayer, source_player)] = (uint8_t) player;
    out[offsetof(MslCoreObservationPlayer, team_relation)] = relation;
    out[offsetof(MslCoreObservationPlayer, team_id)] = fp->team;
    out[offsetof(MslCoreObservationPlayer, char_id)] = fp->kind;
    out[offsetof(MslCoreObservationPlayer, stocks)] =
        (uint8_t) match->source.player.slots[fp->player_id].stocks;
    // Slippi's SendGamePostFrame omits sleeping fighters. Keep roster data,
    // but leave the absent fighter's dynamic fields zeroed.
    if (fp->x221F_b3) {
        return;
    }
    out[offsetof(MslCoreObservationPlayer, present)] = 1;
    put_f32(out, offsetof(MslCoreObservationPlayer, pos_x), pos_x);
    put_f32(out, offsetof(MslCoreObservationPlayer, pos_y), pos_y);
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_air_x_self),
            fp->self_vel.x);
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_ground_x_self),
            fp->gr_vel);
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_y_self),
            fp->self_vel.y);
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_x_attack),
            fp->x8c_kb_vel.x);
    put_f32(out, offsetof(MslCoreObservationPlayer, speed_y_attack),
            fp->x8c_kb_vel.y);
    put_f32(out, offsetof(MslCoreObservationPlayer, percent),
            fp->dmg.x1830_percent);
    put_f32(out, offsetof(MslCoreObservationPlayer, shield_hp),
            fp->shield_health);
    put_u16(out, offsetof(MslCoreObservationPlayer, action_id),
            (uint16_t) fp->motion_id);
    put_u16(out, offsetof(MslCoreObservationPlayer, action_frame),
            (uint16_t) state_age_i16(fp->cur_anim_frame));
    put_u16(out, offsetof(MslCoreObservationPlayer, hitlag),
            float_frames_u16(fp->dmg.x195c_hitlag_frames));
    put_u16(out, offsetof(MslCoreObservationPlayer, hitstun),
            float_frames_u16(hitstun));
    out[offsetof(MslCoreObservationPlayer, facing)] =
        fp->facing_dir > 0.0F;
    out[offsetof(MslCoreObservationPlayer, on_ground)] =
        fp->ground_or_air == GA_Ground;
    out[offsetof(MslCoreObservationPlayer, jumps_left)] =
        jumps_left > 0 ? (uint8_t) jumps_left : 0;
    out[offsetof(MslCoreObservationPlayer, hurtbox_state)] = (uint8_t) hurtbox;
    out[offsetof(MslCoreObservationPlayer, invulnerable)] = hurtbox != 0;
    msl_core_write_stored_fighter(fp, &output->stored);
}

void msl_core_match_write_stored(const MslCoreMatch* match,
                                 MslCoreStoredFrame* output)
{
    memset(output, 0, sizeof(*output));
    for (int player = 0; player < match->config.num_players; ++player) {
        const Fighter* fp = GET_FIGHTER(match->fighters[player]);
        if (!fp->x221F_b3) {
            msl_core_write_stored_fighter(fp, &output->stored[player]);
        }
        if (match->follower_fighters[player] != NULL) {
            const Fighter* follower = GET_FIGHTER(match->follower_fighters[player]);
            if (!follower->x221F_b3) {
                msl_core_write_stored_fighter(follower, &output->follower_stored[player]);
            }
        }
    }
}

// Writes the player into slots[slot] and its follower (Nana) into
// followers[slot]. The follower follows the compare lanes' life-cycle: she is
// absent while asleep before Rebirth.
// refs/melee/src/melee/ft/ftcolanim.c::ftCo_800BFD04
static void write_slot(const MslCoreMatch* match, int player, uint8_t relation,
                       int slot, MslCoreObservation* output)
{
    const Fighter* fp = GET_FIGHTER(match->fighters[player]);
    const Fighter* follower;

    write_fighter(match, fp,
                  match->output_pos_x[player], match->output_pos_y[player],
                  player, relation, &output->slots[slot]);
    if (match->follower_fighters[player] == NULL) {
        return;
    }
    follower = GET_FIGHTER(match->follower_fighters[player]);
    if (follower->x221F_b3) {
        return;
    }
    write_fighter(match, follower, match->follower_output_pos_x[player],
                  match->follower_output_pos_y[player], player, relation,
                  &output->followers[slot]);
}

static void write_follower_from_compare(const MslCoreCompare* compare,
                                        int player, uint8_t relation,
                                        MslCoreObservationPlayer* output)
{
    const uint8_t* source = (const uint8_t*) compare;
    uint8_t* out = (uint8_t*) output;
    uint8_t hurtbox;

    if (!source[offsetof(MslCoreCompare, follower_present) + player]) {
        return;
    }
    hurtbox = source[offsetof(MslCoreCompare, follower_hurtbox_state) + player];
    out[offsetof(MslCoreObservationPlayer, present)] = 1;
    out[offsetof(MslCoreObservationPlayer, source_player)] = (uint8_t) player;
    out[offsetof(MslCoreObservationPlayer, team_relation)] = relation;
    // Slippi has no follower team lane: the follower is on her leader's team.
    out[offsetof(MslCoreObservationPlayer, team_id)] =
        source[offsetof(MslCoreCompare, team_id) + player];
#define COPY_F32(field)                                                       \
    put_f32(out, offsetof(MslCoreObservationPlayer, field),                  \
            compare_player_f32(compare,                                      \
                               offsetof(MslCoreCompare, follower_##field),   \
                               player))
#define COPY_U16(field)                                                       \
    put_u16(out, offsetof(MslCoreObservationPlayer, field),                  \
            compare_player_u16(compare,                                      \
                               offsetof(MslCoreCompare, follower_##field),   \
                               player))
#define COPY_U8(field)                                                        \
    out[offsetof(MslCoreObservationPlayer, field)] =                          \
        source[offsetof(MslCoreCompare, follower_##field) + player]
    COPY_F32(pos_x);
    COPY_F32(pos_y);
    COPY_F32(speed_air_x_self);
    COPY_F32(speed_ground_x_self);
    COPY_F32(speed_y_self);
    COPY_F32(speed_x_attack);
    COPY_F32(speed_y_attack);
    COPY_F32(percent);
    COPY_F32(shield_hp);
    COPY_U16(action_id);
    COPY_U16(action_frame);
    COPY_U16(hitlag);
    COPY_U16(hitstun);
    COPY_U8(char_id);
    COPY_U8(stocks);
    COPY_U8(facing);
    COPY_U8(on_ground);
    COPY_U8(jumps_left);
#undef COPY_F32
#undef COPY_U16
#undef COPY_U8
    out[offsetof(MslCoreObservationPlayer, hurtbox_state)] = hurtbox;
    out[offsetof(MslCoreObservationPlayer, invulnerable)] = hurtbox != 0;
}

int msl_match_randall_position(const MslCoreMatch* match, float* x, float* y)
{
    const MslMpLibState* map = &match->source.mp_lib;
    int i;
    int j;

    if (match->config.stage_id != MSL_CORE_STAGE_YOSHIS_STORY) {
        return 0;
    }
    // Read the collision vertices already published by grStory_801E33E0.
    // The actor origin is below the floor; touching its lazy JObj matrix here
    // could also advance the collision epoch. No binding or mutation is needed.
    for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
        const Ground* ground = &match->stage_ground[i];
        if (!match->stage_ground_used[i] || ground->map_id != 2 ||
            ground->u.randall.jobj == NULL)
        {
            continue;
        }
        for (j = 0; j < map->data->joint_count; ++j) {
            const CollJoint* joint = &map->joints[j];
            const MapJoint* bounds = joint->inner;
            if (joint->x20 == ground->u.randall.jobj && bounds->floor_count > 0) {
                const MapLine* left = map->lines[bounds->floor_start].x0;
                const MapLine* right =
                    map->lines[bounds->floor_start + bounds->floor_count - 1].x0;
                const Vec2* v0 = &map->vertices[left->v0_idx].pos;
                const Vec2* v1 = &map->vertices[right->v1_idx].pos;
                *x = (v0->x + v1->x) * 0.5F;
                *y = (v0->y + v1->y) * 0.5F;
                return 1;
            }
        }
    }
    return 0;
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
    } else if (match->config.stage_id == MSL_STAGE_DREAM_LAND_N64) {
        for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
            const Ground* ground = &match->stage_ground[i];
            if (!match->stage_ground_used[i] || ground->map_id != 7) {
                continue;
            }
            // refs/slippi-ssbm-asm/Recording/Stages/SendDreamlandInfo.asm
            // records actor 7's xDC at 0x80211BF8, the epilogue of
            // grOldPupupu_802113E0. fn_802112F4 consumes the same value:
            // 0=none, 1=left, 2=right.
            // Read the actor, not the optional replay-input override.
            out[offsetof(MslCoreObservationStage, whispy)] =
                (uint8_t) ground->gv.oldpupupu.xDC;
            break;
        }
    } else if (match->config.stage_id == MSL_CORE_STAGE_YOSHIS_STORY) {
        float x;
        float y;
        if (msl_match_randall_position(match, &x, &y)) {
            out[offsetof(MslCoreObservationStage, randall) +
                offsetof(MslCoreObservationRandall, exists)] = 1;
            put_f32(out, offsetof(MslCoreObservationStage, randall) +
                            offsetof(MslCoreObservationRandall, x), x);
            put_f32(out, offsetof(MslCoreObservationStage, randall) +
                            offsetof(MslCoreObservationRandall, y), y);
        }
    }
}

static void canonicalize_production_items(MslCoreItem* items, int count)
{
    int slot;
    for (slot = 0; slot < count; ++slot) {
        MslCoreItem* item = &items[slot];
        uint8_t mask = msl_core_item_gameplay_misc_mask(
            msl_core_get_le16(&item->type), item->state);
        if ((mask & MSL_CORE_ITEM_MISC0) == 0) {
            item->misc0 = 0;
        }
        if ((mask & MSL_CORE_ITEM_MISC1) == 0) {
            item->misc1 = 0;
        }
        if ((mask & MSL_CORE_ITEM_MISC2) == 0) {
            item->misc2 = 0;
        }
        if ((mask & MSL_CORE_ITEM_MISC3) == 0) {
            item->misc3 = 0;
        }
    }
}

void msl_core_canonicalize_production_items(
    MslCoreItem items[MSL_CORE_MAX_ITEMS])
{
    canonicalize_production_items(items, MSL_CORE_MAX_ITEMS);
}

int msl_core_match_write_observation(const MslCoreMatch* match,
                                     uint8_t viewpoint_player,
                                     MslCoreObservation* output)
{
    uint8_t* out;
    uint8_t viewpoint_team;
    int player;
    int slot = 0;
    int item_count;

    if (match == NULL || output == NULL ||
        viewpoint_player >= match->config.num_players)
    {
        return -1;
    }
    out = (uint8_t*) output;
    memset(output, 0, sizeof(*output));
    for (player = 0; player < MSL_CORE_MAX_PLAYERS; ++player) {
        output->slots[player].source_player = UINT8_MAX;
    }
    put_u32(out, offsetof(MslCoreObservation, frame_id),
            (uint32_t) match->frame_id);
    put_u32(out, offsetof(MslCoreObservation, frame_pre_random_seed),
            match->last_frame_seed);
    put_u32(out, offsetof(MslCoreObservation, stage_id),
            match->config.stage_id);
    out[offsetof(MslCoreObservation, num_players)] = match->config.num_players;
    out[offsetof(MslCoreObservation, viewpoint_player)] = viewpoint_player;
    out[offsetof(MslCoreObservation, is_teams)] = match->config.is_teams != 0;
    write_stage(match, &output->stage);

    viewpoint_team =
        GET_FIGHTER(match->fighters[viewpoint_player])->team;
    write_slot(match, viewpoint_player, 0, slot++, output);
    if (match->config.is_teams) {
        for (player = 0; player < match->config.num_players; ++player) {
            if (player != viewpoint_player &&
                GET_FIGHTER(match->fighters[player])->team == viewpoint_team)
            {
                write_slot(match, player, 1, slot++, output);
            }
        }
    }
    for (player = 0; player < match->config.num_players; ++player) {
        if (player != viewpoint_player &&
            (!match->config.is_teams ||
             GET_FIGHTER(match->fighters[player])->team != viewpoint_team))
        {
            write_slot(match, player, 2, slot++, output);
        }
    }
    item_count = msl_core_write_items_into_zeroed(match, output->items);
    canonicalize_production_items(output->items, item_count);
    return 0;
}

static void write_slot_from_compare(const MslCoreCompare* compare,
                                    const MslCoreStoredFrame* stored,
                                    int player, uint8_t relation, int slot,
                                    MslCoreObservation* output)
{
    write_player_from_compare(compare, player, relation, &output->slots[slot]);
    write_follower_from_compare(compare, player, relation, &output->followers[slot]);
    output->slots[slot].stored = stored->stored[player];
    output->followers[slot].stored = stored->follower_stored[player];
}

int msl_core_match_write_observation_from_compare(
    const MslCoreMatch* match, uint8_t viewpoint_player,
    MslCoreObservation* output)
{
    MslCoreStoredFrame stored;
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
    msl_core_match_write_stored(match, &stored);
    source = (const uint8_t*) compare;
    out = (uint8_t*) output;
    memset(output, 0, sizeof(*output));
    for (player = 0; player < MSL_CORE_MAX_PLAYERS; ++player) {
        output->slots[player].source_player = UINT8_MAX;
    }
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
    write_slot_from_compare(compare, &stored, viewpoint_player, 0, slot++, output);
    if (match->config.is_teams) {
        for (player = 0; player < match->config.num_players; ++player) {
            if (player != viewpoint_player &&
                source[offsetof(MslCoreCompare, team_id) + player] ==
                    viewpoint_team)
            {
                write_slot_from_compare(compare, &stored, player, 1, slot++, output);
            }
        }
    }
    for (player = 0; player < match->config.num_players; ++player) {
        if (player != viewpoint_player &&
            (!match->config.is_teams ||
             source[offsetof(MslCoreCompare, team_id) + player] !=
                 viewpoint_team))
        {
            write_slot_from_compare(compare, &stored, player, 2, slot++, output);
        }
    }
    memcpy(out + offsetof(MslCoreObservation, items),
           source + offsetof(MslCoreCompare, items), sizeof(compare->items));
    msl_core_canonicalize_production_items(output->items);
    return 0;
}

void msl_core_match_write_terminal(const MslCoreMatch* match,
                                   int32_t max_frame_id,
                                   MslCoreTerminal* output)
{
    uint8_t* out = (uint8_t*) output;
    uint8_t alive_count = 0;
    uint8_t team_mask = 0;
    uint8_t stockout = 0;
    uint8_t alive_teams = 0;
    uint8_t max_reached;
    int player;

    memset(output, 0, sizeof(*output));
    put_u32(out, offsetof(MslCoreTerminal, frame_id),
            (uint32_t) match->frame_id);
    put_u32(out, offsetof(MslCoreTerminal, stage_id),
            match->config.stage_id);
    for (player = 0; player < match->config.num_players; ++player) {
        const Fighter* fp = GET_FIGHTER(match->fighters[player]);
        uint8_t stocks =
            (uint8_t) match->source.player.slots[fp->player_id].stocks;
        if (stocks == 0) {
            stockout = 1;
        } else {
            uint8_t team_id = fp->team;
            ++alive_count;
            team_mask |= (uint8_t) (1U << (team_id < 8 ? team_id : 7));
        }
    }
    alive_teams = (uint8_t) __builtin_popcount((unsigned int) team_mask);
    max_reached = max_frame_id >= 0 && match->frame_id >= max_frame_id;
    out[offsetof(MslCoreTerminal, done)] = match->rules.ended || max_reached;
    out[offsetof(MslCoreTerminal, match_ended)] = match->rules.ended != 0;
    out[offsetof(MslCoreTerminal, stockout)] = stockout;
    out[offsetof(MslCoreTerminal, max_frame_reached)] = max_reached;
    out[offsetof(MslCoreTerminal, alive_count)] = alive_count;
    out[offsetof(MslCoreTerminal, alive_team_count)] = alive_teams;
    out[offsetof(MslCoreTerminal, team_alive_mask)] = team_mask;
}
