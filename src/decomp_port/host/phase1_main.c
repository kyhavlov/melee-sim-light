#include "host/files.h"
#include "host/wire.h"

#include "ft/fighter.h"
#include "gr/ground.h"
#include "lb/lbarchive.h"
#include "lb/lbspdisplay.h"
#include "mp/mpcoll.h"
#include "mp/mplib.h"
#include "mp/types.h"
#include "pl/player.h"
#include "pl/types.h"

#include <baselib/aobj.h>
#include <baselib/controller.h>
#include <baselib/fobj.h>
#include <baselib/gobj.h>
#include <baselib/id.h>
#include <baselib/list.h>
#include <baselib/mtx.h>
#include <baselib/robj.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MSL_DP_STAGE_FINAL_DESTINATION = 32,
    MSL_DP_CHAR_FOX = 1,
    MSL_DP_STICK_SCALE = 80,
};

extern u32 seed;
extern u32* seed_ptr;

typedef struct Phase1Runtime {
    Fighter_GObj* fighters[2];
    MslDpMatchConfig config;
    int32_t frame_id;
} Phase1Runtime;

static void init_hsd(void)
{
    HSD_GObjLibInitDataType init;

    HSD_AObjInitAllocData();
    HSD_FObjInitAllocData();
    HSD_IDInitAllocData();
    HSD_ListInitAllocData();
    HSD_MtxInitAllocData();
    HSD_VecInitAllocData();
    HSD_RObjInitAllocData();
    HSD_GObj_803912E0(&init);
    init.gproc_pri_max = 0x18;
    HSD_GObj_80391304(&init);

    // Source match bootstrap initializes this fixed fighter-dynamics pool
    // before constructing fighters (refs/melee/src/melee/gm/gm_1832.c).
    lb_8000FCDC();
}

static int read_exact_file(const char* path, void* dst, size_t size)
{
    FILE* file = fopen(path, "rb");
    int trailing;
    if (file == NULL) {
        fprintf(stderr, "could not open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fread(dst, 1, size, file) != size) {
        fprintf(stderr, "%s must contain exactly %lu bytes\n", path,
                (unsigned long) size);
        fclose(file);
        return -1;
    }
    trailing = fgetc(file);
    fclose(file);
    if (trailing != EOF) {
        fprintf(stderr, "%s contains more than one wire row\n", path);
        return -1;
    }
    return 0;
}

static int8_t clamp_stick(int8_t value)
{
    if (value > MSL_DP_STICK_SCALE) {
        return MSL_DP_STICK_SCALE;
    }
    if (value < -MSL_DP_STICK_SCALE) {
        return -MSL_DP_STICK_SCALE;
    }
    return value;
}

static float stick_unit(int8_t value)
{
    return (float) clamp_stick(value) / (float) MSL_DP_STICK_SCALE;
}

static float trigger_unit(uint8_t value)
{
    return (float) value / 255.0F;
}

static uint16_t input_buttons(const MslDpInputPlayer* input)
{
    return msl_dp_get_le16(&input->buttons);
}

static float source_trigger_value(const MslDpInputPlayer* input)
{
    uint16_t buttons = input_buttons(input);
    float value = trigger_unit(input->l > input->r ? input->l : input->r);

    if (value <= p_ftCommonData->x10) {
        value = 0.0F;
    }
    if (buttons & (HSD_PAD_L | HSD_PAD_R)) {
        value = 1.0F;
    } else if (buttons & HSD_PAD_Z) {
        value = p_ftCommonData->x14;
    }
    return value;
}

static uint32_t source_held_buttons(const MslDpInputPlayer* input)
{
    uint32_t buttons = input_buttons(input);
    float trigger = source_trigger_value(input);

    if ((buttons & (HSD_PAD_L | HSD_PAD_R)) || trigger != 0.0F) {
        buttons |= HSD_PAD_LR;
    }
    if (buttons & HSD_PAD_Z) {
        buttons |= HSD_PAD_LR | HSD_PAD_A;
    }
    return buttons;
}

static void inject_pad_status(int slot, const MslDpInputPlayer* input)
{
    HSD_PadStatus* game = &HSD_PadGameStatus[slot];
    HSD_PadStatus* copy = &HSD_PadCopyStatus[slot];
    uint32_t previous = game->button;
    uint32_t buttons = input_buttons(input);

    memset(game, 0, sizeof(*game));
    game->last_button = previous;
    game->button = buttons;
    game->trigger = buttons & (previous ^ buttons);
    game->release = previous & (previous ^ buttons);
    game->stickX = clamp_stick(input->main_x);
    game->stickY = clamp_stick(input->main_y);
    game->subStickX = clamp_stick(input->c_x);
    game->subStickY = clamp_stick(input->c_y);
    game->analogL = input->l;
    game->analogR = input->r;
    game->nml_stickX = stick_unit(input->main_x);
    game->nml_stickY = stick_unit(input->main_y);
    game->nml_subStickX = stick_unit(input->c_x);
    game->nml_subStickY = stick_unit(input->c_y);
    game->nml_analogL = trigger_unit(input->l);
    game->nml_analogR = trigger_unit(input->r);
    *copy = *game;
}

static void seed_previous_input(Fighter* fp, const MslDpInputPlayer* input)
{
    fp->input.x630 = stick_unit(input->main_x);
    fp->input.x634 = stick_unit(input->main_y);
    fp->input.x648 = stick_unit(input->c_x);
    fp->input.x64C = stick_unit(input->c_y);
    fp->input.x658 = source_trigger_value(input);
    fp->input.x664 = source_held_buttons(input);
    fp->x221D_b3 = false;
}

static int decode_config(MslDpMatchConfig* config, const uint8_t* wire)
{
    memset(config, 0, sizeof(*config));
    config->stage_id = msl_dp_get_le32(wire + offsetof(MslDpMatchConfig, stage_id));
    config->frame_id =
        (int32_t) msl_dp_get_le32(wire + offsetof(MslDpMatchConfig, frame_id));
    config->frame_pre_random_seed =
        msl_dp_get_le32(wire + offsetof(MslDpMatchConfig, frame_pre_random_seed));
    config->match_damage_ratio =
        msl_dp_get_lef32(wire + offsetof(MslDpMatchConfig, match_damage_ratio));
    config->num_players = wire[offsetof(MslDpMatchConfig, num_players)];
    config->is_teams = wire[offsetof(MslDpMatchConfig, is_teams)];
    config->stock_count = wire[offsetof(MslDpMatchConfig, stock_count)];
    config->camera_mode = wire[offsetof(MslDpMatchConfig, camera_mode)];
    memcpy(config->players, wire + offsetof(MslDpMatchConfig, players),
           sizeof(config->players));

    if (config->stage_id != MSL_DP_STAGE_FINAL_DESTINATION) {
        fprintf(stderr, "Phase 1 supports stage_id=32 (Final Destination) only\n");
        return -1;
    }
    if (config->num_players != 0 && config->num_players != 2) {
        fprintf(stderr, "Phase 1 requires num_players=2 (or zero default)\n");
        return -1;
    }
    if (config->players[0].char_id != MSL_DP_CHAR_FOX ||
        config->players[1].char_id != MSL_DP_CHAR_FOX)
    {
        fprintf(stderr, "Phase 1 supports two external char_id=1 Fox players only\n");
        return -1;
    }
    config->num_players = 2;
    if (config->stock_count == 0) {
        config->stock_count = 4;
    }
    return 0;
}

static int runtime_init(Phase1Runtime* runtime, const char* data_root,
                        const uint8_t* config_wire,
                        const MslDpInput* previous_input)
{
    HSD_Archive* stage_archive;
    MapCollData* coll_data = NULL;
    UnkStage6B0* ground_param = NULL;
    struct plAllocInfo alloc = { 0 };
    // FD singles slots 0/1 from the source-backed Slippi neutral-spawn table:
    // data/stages/slippi_neutral_spawns.json, extracted from
    // refs/slippi-ssbm-asm/External/NeutralSpawn/NeutralSpawn.asm.
    Vec3 spawns[2] = {
        { -60.0F, 10.0F, 0.0F },
        { 60.0F, 10.0F, 0.0F },
    };
    int i;

    memset(runtime, 0, sizeof(*runtime));
    if (decode_config(&runtime->config, config_wire) != 0) {
        return -1;
    }
    runtime->frame_id = runtime->config.frame_id;

    msl_host_set_data_root(data_root);
    init_hsd();
    stage_archive = lbArchive_LoadSymbols(
        "GrNLa.dat", (void**) &coll_data, "coll_data",
        (void**) &ground_param, "grGroundParam", NULL);
    if (stage_archive == NULL || coll_data == NULL || ground_param == NULL) {
        fprintf(stderr, "failed to load Final Destination data\n");
        return -1;
    }
    stage_info.coll_data = coll_data;
    stage_info.param = ground_param;
    stage_info.internal_stage_id = LAST;
    mpColl_80041C78();
    mpLibLoad(stage_info.coll_data);

    for (i = 0; i < 2; ++i) {
        Player_InitOrResetPlayer(i);
        Player_SetPlayerCharacter(i, CKIND_FOX);
        Player_SetSlottype(i, Gm_PKind_Human);
        Player_SetTeam(i, runtime->config.players[i].team_id);
        Player_SetStocks(i, runtime->config.stock_count);
        Player_SetFacingDirection(i, i == 0 ? 1.0F : -1.0F);
        Player_SetControllerIndex(i, i + 1);
        Player_80032768(i, &spawns[i]);
    }
    Fighter_FirstInitialize_80067A84();

    alloc.internal_id = FTKIND_FOX;
    alloc.x5 = -1;
    for (i = 0; i < 2; ++i) {
        alloc.slot = i;
        runtime->fighters[i] = Fighter_Create(&alloc);
        if (runtime->fighters[i] == NULL) {
            fprintf(stderr, "source Fighter_Create returned NULL for slot %d\n",
                    i);
            return -1;
        }
        seed_previous_input(GET_FIGHTER(runtime->fighters[i]),
                            &previous_input->p[i]);
        inject_pad_status(i, &previous_input->p[i]);
    }

    // MslMatchConfig names this as the seed immediately before the first
    // simulated frame, so constructor-time random choices do not consume it.
    seed = runtime->config.frame_pre_random_seed;
    seed_ptr = &seed;
    return 0;
}

static void put_player_u16(uint8_t* out, size_t field, int player,
                           uint16_t value)
{
    msl_dp_put_le16(out + field + (size_t) player * sizeof(uint16_t), value);
}

static void put_player_u32(uint8_t* out, size_t field, int player,
                           uint32_t value)
{
    msl_dp_put_le32(out + field + (size_t) player * sizeof(uint32_t), value);
}

static void put_player_f32(uint8_t* out, size_t field, int player, float value)
{
    msl_dp_put_lef32(out + field + (size_t) player * sizeof(float), value);
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

static void write_compare(const Phase1Runtime* runtime, uint32_t frame_seed,
                          MslDpCompare* compare)
{
    uint8_t* out = (uint8_t*) compare;
    int i;

    memset(compare, 0, sizeof(*compare));
    msl_dp_put_le32(out + offsetof(MslDpCompare, frame_id),
                    (uint32_t) runtime->frame_id);
    msl_dp_put_le32(out + offsetof(MslDpCompare, frame_pre_random_seed),
                    frame_seed);
    msl_dp_put_le32(out + offsetof(MslDpCompare, stage_id),
                    MSL_DP_STAGE_FINAL_DESTINATION);
    out[offsetof(MslDpCompare, num_players)] = 2;
    out[offsetof(MslDpCompare, is_teams)] = runtime->config.is_teams ? 1 : 0;
    // MslCompare represents all four controller slots. Slots without a source
    // Fighter GObj use the validation contract's inactive/dead value.
    out[offsetof(MslDpCompare, is_dead) + 2] = 1;
    out[offsetof(MslDpCompare, is_dead) + 3] = 1;

    for (i = 0; i < 2; ++i) {
        Fighter* fp = GET_FIGHTER(runtime->fighters[i]);
        uint8_t* fighter_bytes = (uint8_t*) fp;
        int hitstun = (fighter_bytes[0x221C] & 0x02) ? fp->mv.co.common.x0 : 0;
        int hurtbox = fp->x1988 != 0 ? fp->x1988 : fp->x198C;
        int jumps_left = fp->co_attrs.max_jumps - fp->x1968_jumpsUsed;

        out[offsetof(MslDpCompare, team_id) + i] = fp->team;
        out[offsetof(MslDpCompare, char_id) + i] = MSL_DP_CHAR_FOX;
        put_player_f32(out, offsetof(MslDpCompare, pos_x), i, fp->cur_pos.x);
        put_player_f32(out, offsetof(MslDpCompare, pos_y), i, fp->cur_pos.y);
        put_player_f32(out, offsetof(MslDpCompare, speed_air_x_self), i,
                       fp->self_vel.x);
        put_player_f32(out, offsetof(MslDpCompare, speed_ground_x_self), i,
                       fp->gr_vel);
        put_player_f32(out, offsetof(MslDpCompare, speed_y_self), i,
                       fp->self_vel.y);
        put_player_f32(out, offsetof(MslDpCompare, speed_x_attack), i,
                       fp->x8c_kb_vel.x);
        put_player_f32(out, offsetof(MslDpCompare, speed_y_attack), i,
                       fp->x8c_kb_vel.y);
        out[offsetof(MslDpCompare, facing) + i] = fp->facing_dir > 0.0F;
        out[offsetof(MslDpCompare, on_ground) + i] =
            fp->ground_or_air == GA_Ground;
        out[offsetof(MslDpCompare, is_dead) + i] = 0;
        put_player_u16(out, offsetof(MslDpCompare, action_id), i,
                       (uint16_t) fp->motion_id);
        put_player_u16(out, offsetof(MslDpCompare, action_frame), i,
                       (uint16_t) state_age_i16(fp->cur_anim_frame));
        out[offsetof(MslDpCompare, jumps_left) + i] =
            jumps_left > 0 ? (uint8_t) jumps_left : 0;
        out[offsetof(MslDpCompare, stocks) + i] =
            (uint8_t) Player_GetStocks(fp->player_id);
        put_player_f32(out, offsetof(MslDpCompare, percent), i,
                       fp->dmg.x1830_percent);
        put_player_f32(out, offsetof(MslDpCompare, shield_hp), i,
                       fp->shield_health);
        put_player_u16(out, offsetof(MslDpCompare, hitlag), i,
                       float_frames_u16(fp->dmg.x195c_hitlag_frames));
        put_player_u16(out, offsetof(MslDpCompare, hitstun), i,
                       hitstun > 0 ? (uint16_t) hitstun : 0);
        out[offsetof(MslDpCompare, l_cancel) + i] = 0;
        out[offsetof(MslDpCompare, hurtbox_state) + i] = (uint8_t) hurtbox;
        put_player_u16(out, offsetof(MslDpCompare, ground_id), i,
                       (uint16_t) fp->coll_data.floor.index);
        put_player_u32(out, offsetof(MslDpCompare, animation_index), i,
                       (uint32_t) fp->anim_id);
        put_player_u16(out, offsetof(MslDpCompare, instance_hit_by), i,
                       fp->dmg.x18ec_instancehitby);
        put_player_u16(out, offsetof(MslDpCompare, instance_id), i,
                       fp->x2074.x2088);
        out[offsetof(MslDpCompare, last_attack_landed) + i] =
            (uint8_t) fp->x208C;
        out[offsetof(MslDpCompare, combo_count) + i] = (uint8_t) fp->x2090;
        out[offsetof(MslDpCompare, last_hit_by) + i] =
            (uint8_t) fp->dmg.x18c4_source_ply;
        out[offsetof(MslDpCompare, state_flags) + i * 5 + 0] =
            fighter_bytes[0x2218];
        out[offsetof(MslDpCompare, state_flags) + i * 5 + 1] =
            fighter_bytes[0x221A];
        out[offsetof(MslDpCompare, state_flags) + i * 5 + 2] =
            fighter_bytes[0x221B];
        out[offsetof(MslDpCompare, state_flags) + i * 5 + 3] =
            fighter_bytes[0x221C];
        out[offsetof(MslDpCompare, state_flags) + i * 5 + 4] =
            fighter_bytes[0x221F];
    }
}

static int runtime_step(Phase1Runtime* runtime, const MslDpInput* input,
                        FILE* output)
{
    MslDpCompare compare;
    uint32_t frame_seed = *seed_ptr;
    int i;

    for (i = 0; i < 2; ++i) {
        inject_pad_status(i, &input->p[i]);
    }
    HSD_GObj_80390CFC();
    runtime->frame_id += 1;
    write_compare(runtime, frame_seed, &compare);
    if (fwrite(&compare, 1, sizeof(compare), output) != sizeof(compare)) {
        fprintf(stderr, "failed to write compare output: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s GAME_DATA CONFIG PREV_INPUT_OR_- INPUT_TAPE OUTPUT\n"
            "       %s GAME_DATA --stream\n",
            argv0, argv0);
}

static int run_stream(const char* data_root)
{
    uint8_t config_wire[sizeof(MslDpMatchConfig)];
    MslDpInput previous_input;
    MslDpInput input;
    Phase1Runtime runtime;
    size_t count;

    if (fread(config_wire, 1, sizeof(config_wire), stdin) !=
            sizeof(config_wire) ||
        fread(&previous_input, 1, sizeof(previous_input), stdin) !=
            sizeof(previous_input)) {
        fprintf(stderr, "short decomp-port stream header\n");
        return 1;
    }
    if (runtime_init(&runtime, data_root, config_wire, &previous_input) != 0) {
        return 1;
    }
    while ((count = fread(&input, 1, sizeof(input), stdin)) == sizeof(input)) {
        if (runtime_step(&runtime, &input, stdout) != 0) {
            return 1;
        }
    }
    if (count != 0 || ferror(stdin)) {
        fprintf(stderr, "short decomp-port input stream\n");
        return 1;
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "failed to flush compare stream: %s\n",
                strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    uint8_t config_wire[sizeof(MslDpMatchConfig)];
    MslDpInput previous_input;
    MslDpInput input;
    Phase1Runtime runtime;
    FILE* input_file;
    FILE* output_file;
    size_t count;
    int result = 1;

    if (argc == 3 && strcmp(argv[2], "--stream") == 0) {
        return run_stream(argv[1]);
    }
    if (argc != 6) {
        usage(argv[0]);
        return 2;
    }
    if (read_exact_file(argv[2], config_wire, sizeof(config_wire)) != 0) {
        return 2;
    }
    memset(&previous_input, 0, sizeof(previous_input));
    if (strcmp(argv[3], "-") != 0 &&
        read_exact_file(argv[3], &previous_input, sizeof(previous_input)) != 0)
    {
        return 2;
    }
    input_file = fopen(argv[4], "rb");
    if (input_file == NULL) {
        fprintf(stderr, "could not open %s: %s\n", argv[4], strerror(errno));
        return 2;
    }
    output_file = fopen(argv[5], "wb");
    if (output_file == NULL) {
        fprintf(stderr, "could not open %s: %s\n", argv[5], strerror(errno));
        fclose(input_file);
        return 2;
    }
    if (runtime_init(&runtime, argv[1], config_wire, &previous_input) != 0) {
        goto done;
    }

    while ((count = fread(&input, 1, sizeof(input), input_file)) == sizeof(input)) {
        if (runtime_step(&runtime, &input, output_file) != 0) {
            goto done;
        }
    }
    if (count != 0 || ferror(input_file)) {
        fprintf(stderr, "%s is not a whole-number sequence of MslInput rows\n",
                argv[4]);
        goto done;
    }
    result = 0;

done:
    if (fclose(output_file) != 0) {
        result = 1;
    }
    fclose(input_file);
    return result;
}
