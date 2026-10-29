#include "runtime/observation.h"

#include "ft/chara/ftCommon/forward.h"
#include "ft/chara/ftPeach/forward.h"
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
}

// The charge a fighter keeps between moves, as the game counts it. These are
// the fighters ftData_UnkMotionStates4 gives a full-charge overlay, read from
// the variable each of those callbacks tests; Kirby's is the one his copied
// ability owns.
// refs/melee/src/melee/ft/ftdata.c::ftData_UnkMotionStates4
static uint8_t stored_charge(const Fighter* fp)
{
    int charge = 0;

    switch (fp->kind) {
    case FTKIND_DONKEY:
        charge = fp->fv.dk.x222C;
        break;
    case FTKIND_SAMUS:
        charge = fp->fv.ss.x2230;
        break;
    case FTKIND_MEWTWO:
        charge = fp->fv.mt.x2234_shadowBallCharge;
        break;
    case FTKIND_SEAK:
        charge = fp->fv.sk.x0;
        break;
    case FTKIND_GAMEWATCH:
        charge = fp->fv.gw.x2238_panicCharge;
        break;
    case FTKIND_KIRBY:
        // refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::ftKb_Init_UnkMotionStates4
        switch (fp->fv.kb.hat.kind) {
        case FTKIND_DONKEY:
            charge = fp->fv.kb.xBC;
            break;
        case FTKIND_SAMUS:
            charge = fp->fv.kb.xA8;
            break;
        case FTKIND_MEWTWO:
            charge = fp->fv.kb.x9C;
            break;
        case FTKIND_SEAK:
            charge = fp->fv.kb.xB4;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return charge < 0 ? 0 : charge > UINT8_MAX ? UINT8_MAX : (uint8_t) charge;
}

// Stored amounts that are not a count, as the game holds them.
// Oil Panic: gauge[0] is the damage the caught shots would have dealt, summed
// by ftGw_SpecialLw_AbsorbThink_DecideAction. The spill deals a multiple of
// it, and ftGw_Init_OnDeath clears it while leaving the count.
// refs/melee/src/melee/ft/chara/ftGameWatch/ftGw_SpecialLw.c::ftGw_SpecialLwShoot_ReleaseOil
// Fire Breath: gauge[0] is the fuel and gauge[1] the flame size. Each breath
// frame takes 1 from both down to a floor, and every frame outside the move
// gives a little back up to the full value (ftKp_SpecialLw_80134D78). Each
// flame is spawned with both: its speed is the fuel over the full fuel and
// its scale the size over the full size (itKoopaFlame_Spawn). Kirby's copy
// keeps its own pair while he wears Bowser's hat.
// refs/melee/src/melee/ft/chara/ftKoopa/ftKp_SpecialN.c::ftKp_SpecialN_IASA
// refs/melee/src/melee/ft/chara/ftKirby/ftkirbyspecialkoopa.c::ftKb_SpecialNKp_800FA7D4
// Peach's float: gauge[0] is the frames of float left. The game loads it when
// a float starts, counts it down in the float and in the attacks done out of
// it, and reads it nowhere else, so a float that ends early leaves the rest
// behind; outside those states it is published as 0.
// refs/melee/src/melee/ft/chara/ftPeach/ftPe_Float.c::ftPe_Float_Anim
// refs/melee/src/melee/ft/chara/ftPeach/ftPe_FloatAttack.c::ftPe_FloatAttackAir_Anim
static void stored_gauge(const Fighter* fp, float gauge[2])
{
    gauge[0] = 0.0F;
    gauge[1] = 0.0F;
    switch (fp->kind) {
    case FTKIND_GAMEWATCH:
        gauge[0] = (float) fp->fv.gw.x223C_panicDamage;
        break;
    case FTKIND_KOOPA:
        gauge[0] = fp->fv.kp.x222C;
        gauge[1] = fp->fv.kp.x2230;
        break;
    case FTKIND_PEACH:
        if (fp->motion_id == ftPe_MS_Float ||
            (fp->motion_id >= ftPe_MS_FloatAttackAirN &&
             fp->motion_id <= ftPe_MS_FloatAttackAirLw))
        {
            gauge[0] = fp->fv.pe.x4;
        }
        break;
    case FTKIND_KIRBY:
        if (fp->fv.kb.hat.kind == FTKIND_KOOPA) {
            gauge[0] = fp->fv.kb.x84;
            gauge[1] = fp->fv.kb.x88;
        }
        break;
    default:
        break;
    }
}

// Kirby's copied ability as the fighter kind whose neutral special he has,
// the id space of char_id. The game keeps FTKIND_KIRBY for no hat
// (ftKb_SpecialN_800F5D04); that and every other fighter read
// MSL_COPIED_NONE.
// refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::ftKb_SpecialN_800F1BAC
static uint8_t copied_char(const Fighter* fp)
{
    if (fp->kind != FTKIND_KIRBY || fp->fv.kb.hat.kind == FTKIND_KIRBY) {
        return MSL_COPIED_NONE;
    }
    return (uint8_t) fp->fv.kb.hat.kind;
}

// What the fighter has used and the game has not yet given back. Each lift
// bit is the variable the special tests before it rises, named by the
// special's direction; Kirby's neutral one belongs to the copied move. The
// game clears most of them in ftCo_Landing_Enter and in the fighter's
// OnDeath; Luigi's only when a cyclone touches the ground.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter
// refs/melee/src/melee/ft/chara/ftLuigi/ftLg_SpecialLw.c::ftLg_SpecialAirLw_Phys
// Peach's float is spent while has_float is clear: the float clears it and
// any action change on the ground sets it, where the tether flag is cleared.
// refs/melee/src/melee/ft/chara/ftPeach/ftPe_Float.c::ftPe_8011BB6C
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_AirCatch.c::ftCo_800C3B10
// refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
static uint8_t spent_flags(const Fighter* fp, const Fighter* follower)
{
    uint8_t spent = fp->used_tether ? MSL_SPENT_TETHER : 0;

    switch (fp->kind) {
    case FTKIND_MARIO:
    case FTKIND_DRMARIO:
        spent |= fp->fv.mr.x2238_isCapeBoost ? MSL_SPENT_SIDE_LIFT : 0;
        spent |= fp->fv.mr.x2234_tornadoCharge ? MSL_SPENT_DOWN_LIFT : 0;
        break;
    case FTKIND_LUIGI:
        spent |= fp->fv.lg.x222C_cycloneCharge ? MSL_SPENT_DOWN_LIFT : 0;
        break;
    case FTKIND_MARS:
    case FTKIND_EMBLEM:
        spent |= fp->fv.ms.x222C ? MSL_SPENT_SIDE_LIFT : 0;
        break;
    case FTKIND_MEWTWO:
        spent |= fp->fv.mt.x223C_isConfusionBoost ? MSL_SPENT_SIDE_LIFT : 0;
        break;
    case FTKIND_PEACH:
        spent |= fp->fv.pe.specialairn_used ? MSL_SPENT_NEUTRAL_LIFT : 0;
        spent |= fp->fv.pe.has_float ? 0 : MSL_SPENT_FLOAT;
        break;
    case FTKIND_POPO:
    case FTKIND_NANA:
        spent |= fp->fv.pp.x224C ? MSL_SPENT_NEUTRAL_LIFT : 0;
        break;
    case FTKIND_KIRBY:
        spent |= fp->fv.kb.x64 ? MSL_SPENT_SIDE_LIFT : 0;
        switch (fp->fv.kb.hat.kind) {
        case FTKIND_POPO:
        case FTKIND_NANA:
            spent |= fp->fv.kb.xC4 ? MSL_SPENT_NEUTRAL_LIFT : 0;
            break;
        case FTKIND_PEACH:
            spent |= fp->fv.kb.xCC ? MSL_SPENT_NEUTRAL_LIFT : 0;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    if (follower != NULL && !follower->x221F_b3 && follower->fv.nn.x224C) {
        spent |= MSL_SPENT_FOLLOWER_NEUTRAL_LIFT;
    }
    return spent;
}

// Mr. Game & Watch's last two Judge numbers, as the game holds them: the roll
// leaves both out, then moves the newer into the older and stores the new one.
// refs/melee/src/melee/ft/chara/ftGameWatch/ftGw_SpecialS.c::ftGw_SpecialS_GetRandomInt
static uint8_t judge_number(const Fighter* fp, int older)
{
    if (fp->kind != FTKIND_GAMEWATCH) {
        return MSL_JUDGE_NONE;
    }
    return (uint8_t) (older ? fp->fv.gw.x2230_judgeVar2
                            : fp->fv.gw.x222C_judgeVar1);
}

// Writes what the player in slots[slot] keeps between moves. Slippi records
// none of it, so there is no compare lane: both observation builders read
// the fighter here. An absent player keeps the empty record.
static void write_stored(const MslCoreMatch* match, int player, int slot,
                         MslCoreObservation* output)
{
    const Fighter* fp = GET_FIGHTER(match->fighters[player]);
    const Fighter* follower = match->follower_fighters[player] == NULL
                                  ? NULL
                                  : GET_FIGHTER(match->follower_fighters[player]);
    uint8_t* out = (uint8_t*) &output->stored[slot];
    float gauge[2];

    if (fp->x221F_b3) {
        return;
    }
    out[offsetof(MslCoreObservationStored, charge)] = stored_charge(fp);
    out[offsetof(MslCoreObservationStored, copied_char)] = copied_char(fp);
    out[offsetof(MslCoreObservationStored, spent)] = spent_flags(fp, follower);
    // The wall jumps since the fighter last stood: each one after the first
    // rises less.
    // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_Anim
    out[offsetof(MslCoreObservationStored, wall_jumps)] = fp->x1969_walljumpUsed;
    out[offsetof(MslCoreObservationStored, judge)] = judge_number(fp, 0);
    out[offsetof(MslCoreObservationStored, judge) + 1] = judge_number(fp, 1);
    stored_gauge(fp, gauge);
    put_f32(out, offsetof(MslCoreObservationStored, gauge), gauge[0]);
    put_f32(out, offsetof(MslCoreObservationStored, gauge) + sizeof(float),
            gauge[1]);
}

// Writes the player into slots[slot] and its follower (Nana) into
// followers[slot]. The follower follows the compare lanes' life-cycle: she is
// absent while asleep before Rebirth.
// refs/melee/src/melee/ft/ftcolanim.c::ftCo_800BFD04
static void write_slot(const MslCoreMatch* match, int player, uint8_t relation,
                       int slot, MslCoreObservation* output)
{
    const Fighter* follower;

    write_fighter(match, GET_FIGHTER(match->fighters[player]),
                  match->output_pos_x[player], match->output_pos_y[player],
                  player, relation, &output->slots[slot]);
    write_stored(match, player, slot, output);
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
        output->stored[player].copied_char = MSL_COPIED_NONE;
        output->stored[player].judge[0] = MSL_JUDGE_NONE;
        output->stored[player].judge[1] = MSL_JUDGE_NONE;
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

int msl_core_match_write_observation_from_compare(
    const MslCoreMatch* match, uint8_t viewpoint_player,
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
    for (player = 0; player < MSL_CORE_MAX_PLAYERS; ++player) {
        output->slots[player].source_player = UINT8_MAX;
        output->stored[player].copied_char = MSL_COPIED_NONE;
        output->stored[player].judge[0] = MSL_JUDGE_NONE;
        output->stored[player].judge[1] = MSL_JUDGE_NONE;
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
    write_stored(match, viewpoint_player, slot, output);
    write_follower_from_compare(compare, viewpoint_player, 0,
                                &output->followers[slot]);
    write_player_from_compare(compare, viewpoint_player, 0,
                              &output->slots[slot++]);
    if (match->config.is_teams) {
        for (player = 0; player < match->config.num_players; ++player) {
            if (player != viewpoint_player &&
                source[offsetof(MslCoreCompare, team_id) + player] ==
                    viewpoint_team)
            {
                write_stored(match, player, slot, output);
                write_follower_from_compare(compare, player, 1,
                                            &output->followers[slot]);
                write_player_from_compare(compare, player, 1,
                                          &output->slots[slot++]);
            }
        }
    }
    for (player = 0; player < match->config.num_players; ++player) {
        if (player != viewpoint_player &&
            (!match->config.is_teams ||
             source[offsetof(MslCoreCompare, team_id) + player] !=
                 viewpoint_team))
        {
            write_stored(match, player, slot, output);
            write_follower_from_compare(compare, player, 2,
                                        &output->followers[slot]);
            write_player_from_compare(compare, player, 2,
                                      &output->slots[slot++]);
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
