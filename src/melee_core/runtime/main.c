#include "platform/files.h"
#include "runtime/match.h"
#include "runtime/effects.h"
#include "runtime/final_destination.h"
#include "runtime/wire.h"

#include "ft/fighter.h"
#include "ft/ftdevice.h"
#include "ft/ftlib.h"
#include "gr/grdatfiles.h"
#include "gr/ground.h"
#include "gr/types.h"
#include "it/inlines.h"
#include "it/item.h"
#include "it/types.h"
#include "lb/lbarchive.h"
#include "lb/lbspdisplay.h"
#include "mp/mpcoll.h"
#include "mp/mplib.h"
#include "mp/types.h"
#include "pl/player.h"
#include "pl/types.h"

#include <MSL/math.h>

#include <baselib/aobj.h>
#include <baselib/controller.h>
#include <baselib/fobj.h>
#include <baselib/gobj.h>
#include <baselib/gobjuserdata.h>
#include <baselib/id.h>
#include <baselib/jobj.h>
#include <baselib/list.h>
#include <baselib/mtx.h>
#include <baselib/robj.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MSL_CORE_STAGE_FINAL_DESTINATION = 32,
    MSL_CORE_CHAR_FOX = 1,
    MSL_CORE_STICK_SCALE = 80,
    MSL_CORE_FD_MAP_GOBJ_COUNT = 10,
};

extern u32 seed;
extern u32* seed_ptr;
extern int mpColl_804D64AC;
extern void msl_camera_publish_fighter_visibility(HSD_GObj* gobj);
extern void Camera_8002B3D4(void* arg0);

typedef struct MslCoreRuntime {
    Fighter_GObj* fighters[2];
    Ground stage_ground[MSL_CORE_FD_MAP_GOBJ_COUNT];
    uint8_t source_slots[2];
    MslCoreMatchConfig config;
    int32_t frame_id;
} MslCoreRuntime;

static void headless_ground_anim_proc(HSD_GObj* gobj)
{
    // Exact gameplay-bearing projection of Ground_801C1CD0. Material updates
    // and the per-stage presentation callback are absent on FD, but every map
    // GObj must still publish one collision epoch after advancing its JObj.
    // refs/melee/src/melee/gr/ground.c::Ground_801C1CD0
    HSD_JObjAnimAll(gobj->hsd_obj);
    mpColl_804D64AC += 1;
}

static void init_hsd(void)
{
    HSD_GObjLibInitDataType init;

    // The DOL runs MSL's constructor table before scene bootstrap. The ELF
    // host build has no MetroWerks .ctors section, so initialize trigf's
    // split 4/pi constants explicitly before any gameplay math.
    __sinit_trigf_c();
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

static void source_clamp_stick(int8_t raw_x, int8_t raw_y, int8_t* out_x,
                               int8_t* out_y)
{
    float radius;
    int8_t x = raw_x;
    int8_t y = raw_y;

    // Exact default-min/default-max projection of HSD_PadClampCheck3 from
    // refs/melee/src/sysdolphin/baselib/controller.c. The assignments back
    // to signed bytes intentionally truncate each radially scaled axis.
    radius = sqrtf(((float) x * (float) x) + ((float) y * (float) y));
    if (radius > (float) MSL_CORE_STICK_SCALE) {
        x = ((float) x * (float) MSL_CORE_STICK_SCALE) / radius;
        y = ((float) y * (float) MSL_CORE_STICK_SCALE) / radius;
    }
    *out_x = x;
    *out_y = y;
}

static float trigger_unit(uint8_t value)
{
    // The Slippi physical-trigger fields are HSD_PadStatus::nml_analog{L,R},
    // whose source scale is 140 after Melee's controller clamp.
    // refs/melee/src/{melee/gm/gmmain.c::gmMain_8015FD24,
    // sysdolphin/baselib/controller.c::HSD_PadScale}
    return (float) value / 140.0F;
}

static uint16_t input_buttons(const MslCoreInputPlayer* input)
{
    return msl_core_get_le16(&input->buttons);
}

static float source_trigger_value(const MslCoreInputPlayer* input)
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

static uint32_t source_held_buttons(const MslCoreInputPlayer* input)
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

static void inject_pad_status(int slot, const MslCoreInputPlayer* input)
{
    HSD_PadStatus* game = &HSD_PadGameStatus[slot];
    HSD_PadStatus* copy = &HSD_PadCopyStatus[slot];
    int8_t main_x;
    int8_t main_y;
    int8_t c_x;
    int8_t c_y;
    uint32_t previous = game->button;
    uint32_t buttons = input_buttons(input);

    source_clamp_stick(input->main_x, input->main_y, &main_x, &main_y);
    source_clamp_stick(input->c_x, input->c_y, &c_x, &c_y);
    memset(game, 0, sizeof(*game));
    game->last_button = previous;
    game->button = buttons;
    game->trigger = buttons & (previous ^ buttons);
    game->release = previous & (previous ^ buttons);
    game->stickX = main_x;
    game->stickY = main_y;
    game->subStickX = c_x;
    game->subStickY = c_y;
    game->analogL = input->l;
    game->analogR = input->r;
    game->nml_stickX = (float) main_x / (float) MSL_CORE_STICK_SCALE;
    game->nml_stickY = (float) main_y / (float) MSL_CORE_STICK_SCALE;
    game->nml_subStickX = (float) c_x / (float) MSL_CORE_STICK_SCALE;
    game->nml_subStickY = (float) c_y / (float) MSL_CORE_STICK_SCALE;
    game->nml_analogL = trigger_unit(input->l);
    game->nml_analogR = trigger_unit(input->r);
    *copy = *game;
}

static void seed_previous_input(Fighter* fp, const MslCoreInputPlayer* input)
{
    int8_t main_x;
    int8_t main_y;
    int8_t c_x;
    int8_t c_y;

    source_clamp_stick(input->main_x, input->main_y, &main_x, &main_y);
    source_clamp_stick(input->c_x, input->c_y, &c_x, &c_y);
    fp->input.x630 = (float) main_x / (float) MSL_CORE_STICK_SCALE;
    fp->input.x634 = (float) main_y / (float) MSL_CORE_STICK_SCALE;
    fp->input.x648 = (float) c_x / (float) MSL_CORE_STICK_SCALE;
    fp->input.x64C = (float) c_y / (float) MSL_CORE_STICK_SCALE;
    fp->input.x658 = source_trigger_value(input);
    fp->input.x664 = source_held_buttons(input);
    fp->x221D_b3 = false;
}

static int decode_config(MslCoreMatchConfig* config, const uint8_t* wire)
{
    memset(config, 0, sizeof(*config));
    config->stage_id = msl_core_get_le32(wire + offsetof(MslCoreMatchConfig, stage_id));
    config->frame_id =
        (int32_t) msl_core_get_le32(wire + offsetof(MslCoreMatchConfig, frame_id));
    config->frame_pre_random_seed =
        msl_core_get_le32(wire + offsetof(MslCoreMatchConfig, frame_pre_random_seed));
    config->match_damage_ratio =
        msl_core_get_lef32(wire + offsetof(MslCoreMatchConfig, match_damage_ratio));
    config->num_players = wire[offsetof(MslCoreMatchConfig, num_players)];
    config->is_teams = wire[offsetof(MslCoreMatchConfig, is_teams)];
    config->stock_count = wire[offsetof(MslCoreMatchConfig, stock_count)];
    config->camera_mode = wire[offsetof(MslCoreMatchConfig, camera_mode)];
    config->online_fnmsubs_zero =
        wire[offsetof(MslCoreMatchConfig, online_fnmsubs_zero)];
    config->brawl_offscreen_damage =
        wire[offsetof(MslCoreMatchConfig, brawl_offscreen_damage)];
    memcpy(config->players, wire + offsetof(MslCoreMatchConfig, players),
           sizeof(config->players));

    if (config->stage_id != MSL_CORE_STAGE_FINAL_DESTINATION) {
        fprintf(stderr, "current core supports stage_id=32 (Final Destination) only\n");
        return -1;
    }
    if (config->num_players != 0 && config->num_players != 2) {
        fprintf(stderr, "current core requires num_players=2 (or zero default)\n");
        return -1;
    }
    if (config->players[0].char_id != MSL_CORE_CHAR_FOX ||
        config->players[1].char_id != MSL_CORE_CHAR_FOX)
    {
        fprintf(stderr, "current core supports two external char_id=1 Fox players only\n");
        return -1;
    }
    config->num_players = 2;
    if (config->stock_count == 0) {
        config->stock_count = 4;
    }
    return 0;
}

static int runtime_init(MslCoreRuntime* runtime, const char* data_root,
                        const uint8_t* config_wire,
                        const MslCoreInput* previous_input)
{
    UnkArchiveStruct* stage_data;
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
    for (i = 0; i < 2; ++i) {
        uint8_t encoded = runtime->config.players[i].facing_and_port;
        uint8_t port = encoded >> 1;
        runtime->source_slots[i] = port == 0 ? (uint8_t) i : (uint8_t) (port - 1);
        if (runtime->source_slots[i] >= MSL_CORE_MAX_PLAYERS ||
            (i != 0 && runtime->source_slots[i] == runtime->source_slots[0]))
        {
            fprintf(stderr, "invalid two-player physical port mapping\n");
            return -1;
        }
    }
    msl_core_set_match_rules(runtime->config.is_teams,
                               runtime->config.match_damage_ratio,
                               runtime->config.online_fnmsubs_zero,
                               runtime->config.brawl_offscreen_damage);

    msl_host_set_data_root(data_root);
    init_hsd();
    msl_effect_projection_init();
    // Standard versus creates the source camera owner before stage/fighters.
    // The HSD CObj/draw link remains headless; the transform process and
    // subject pool are gameplay-relevant through fp->x221F_b0.
    Camera_80028B9C(0x46);
    {
        HSD_GObj* camera_gobj = GObj_Create(0x10, 0x12, 0);
        if (camera_gobj == NULL) {
            fprintf(stderr, "failed to create headless gameplay camera GObj\n");
            return -1;
        }
        HSD_GObj_SetupProc(camera_gobj,
                           (void (*)(HSD_GObj*)) Camera_8002B3D4, 0x12);
    }
    // Source stage-data bootstrap without renderer-owned Ground GObjs.
    // grDatFiles publishes every gameplay DAT symbol; the four FD map joints
    // below publish map-point JObjs exactly as Ground_GetStageGObj does.
    Ground_801BFFB0();
    stage_info.internal_stage_id = LAST;
    grDatFiles_801C6038("/GrNLa.dat", 0, 0);
    if (stage_info.coll_data == NULL || stage_info.param == NULL) {
        fprintf(stderr, "failed to load Final Destination data\n");
        return -1;
    }
    // Stage_802251E8 keeps the external stage-list id (32 for FD) separately
    // from InternalStageId::LAST; grGroundParam is keyed by that list id.
    Ground_801C28CC(&stage_info.xA0, MSL_CORE_STAGE_FINAL_DESTINATION);

    Ground_801C38D0(stage_info.param->x8, stage_info.param->x14,
                    stage_info.param->x1C, stage_info.param->x18);
    Ground_801C38EC(stage_info.param->x10, stage_info.param->xC);
    Ground_801C3970(stage_info.param->x28);
    Ground_801C3900(stage_info.param->x2E, stage_info.param->x30,
                    stage_info.param->x34, stage_info.param->x38,
                    stage_info.param->x3C, stage_info.param->x40,
                    stage_info.param->x44, stage_info.param->x48);
    Ground_801C392C(stage_info.param->x50, stage_info.param->x54,
                    stage_info.param->x58, stage_info.param->x5C,
                    stage_info.param->x60, stage_info.param->x64);
    Ground_801C3960(stage_info.param->x20);
    Ground_801C3950(stage_info.param->x24);

    mpColl_80041C78();
    mpLibLoad(stage_info.coll_data);
    mpLib_80058820();
    stage_data = grDatFiles_801C6324();
    if (stage_data == NULL || stage_data->unk4 == NULL) {
        fprintf(stderr, "Final Destination map data is missing\n");
        return -1;
    }
    if (stage_data->unk4->unkC != MSL_CORE_FD_MAP_GOBJ_COUNT) {
        fprintf(stderr, "unexpected Final Destination map GObj count %d\n",
                stage_data->unk4->unkC);
        return -1;
    }
    // Playback/Core/RestoreGameInfo.asm restores the replay's initial RNG
    // before stage construction. grLast_8021AC30 consumes that stream to
    // initialize the background-rotation state, while the first frame-start
    // process restores the same seed again before gameplay callbacks.
    seed = runtime->config.frame_pre_random_seed;
    seed_ptr = &seed;
    for (i = 0; i < stage_data->unk4->unkC; ++i) {
        HSD_GObj* gobj;
        HSD_JObj* root;
        Ground* gp = &runtime->stage_ground[i];
        stage_data = grDatFiles_801C6330(i);
        if (stage_data == NULL || stage_data->unk4 == NULL ||
            i >= stage_data->unk4->unkC ||
            stage_data->unk4->unk8[i].unk0 == NULL)
        {
            fprintf(stderr, "Final Destination map joint %d is missing\n", i);
            return -1;
        }
        root = HSD_JObjLoadJoint(stage_data->unk4->unk8[i].unk0);
        if (root == NULL) {
            fprintf(stderr, "failed to load Final Destination map joint %d\n",
                    i);
            return -1;
        }
        Ground_801C34AC(i, root, stage_data->unk4->unk8[i].unk0);
        gobj = GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0);
        if (gobj == NULL) {
            fprintf(stderr, "failed to create Final Destination map GObj %d\n",
                    i);
            return -1;
        }
        gp->map_id = i;
        gp->gobj = gobj;
        gp->x10_flags.b2 = true;
        memset(gp->x20, 0xFF, sizeof(gp->x20));
        GObj_InitUserData(gobj, 3, NULL, gp);
        HSD_GObjObject_80390A70(gobj, HSD_GObj_804D7849, root);
        HSD_GObj_SetupProc(gobj, headless_ground_anim_proc, 1);
        if (i == 7) {
            msl_fd_background_init(gobj);
        }
    }
    // grlast.c::grLast_OnInit gameplay-visible stage publication.
    stage_info.unk8C.b4 = true;
    stage_info.unk8C.b5 = true;
    Ground_801C39C0();
    Ground_801C3BB4();
    // refs/melee/src/melee/gm/gm_16AE.c::fn_8016DCC0 and fn_8016E730.
    // Keep the source owners intact: Player_InitAllPlayers also initializes
    // each slot's statistics state, and Player_80036DD8 loads the common
    // player table consumed by the scheduled statistics pass.
    Player_InitAllPlayers();
    Player_80036DD8();
    for (i = 0; i < 2; ++i) {
        int slot = runtime->source_slots[i];
        uint8_t encoded = runtime->config.players[i].facing_and_port;
        float facing = (encoded >> 1) == 0 ? (i == 0 ? 1.0F : -1.0F)
                                            : ((encoded & 1) ? 1.0F : -1.0F);
        Player_SetPlayerCharacter(slot, CKIND_FOX);
        Player_SetSlottype(slot, Gm_PKind_Human);
        Player_SetTeam(slot, runtime->config.players[i].team_id);
        Player_SetStocks(slot, runtime->config.stock_count);
        Player_SetCostumeId(slot, runtime->config.players[i].costume_id);
        Player_SetPlayerId(slot, slot);
        Player_SetFacingDirection(slot, facing);
        Player_SetControllerIndex(slot, slot + 1);
        // Standard VS PlayerInitData leaves xD_b2 clear, enabling magnify
        // damage for ordinary human fighters.
        // refs/melee/src/melee/gm/gm_16AE.c::fn_8016D8AC
        Player_SetMoreFlagsBit3(slot, 1);
        // PlayerInitData.xC_b1 selects the normal versus-entry creation path.
        // Standard versus starts assign staggered five-frame Entry timers.
        // refs/melee/src/melee/gm/gm_16AE.c::fn_8016D8AC
        Player_SetFlagsBit3(slot, 1);
        Player_SetUnk4C(slot, (slot + 1) * 5);
        Player_80032768(slot, &spawns[i]);
    }
    // The versus bootstrap initializes fighter/device/item allocation before
    // Fighter_Create. Items are disabled in the current Fox/FD domain, so the exact
    // Item_80266FA8(false) entry is equivalent to Item_80266F70's source gate
    // without retaining the scene-owned item-switch aggregate.
    ftCo_800C06C0();
    Item_80266FA8();
    Item_80266FCC();
    Player_80036DA4();
    for (i = 0; i < 2; ++i) {
        int slot = runtime->source_slots[i];
        // gm_16AE.c::fn_8016E2BC creates match fighters through the player
        // owner so player_entity/transformation state and scheduled player
        // bookkeeping refer to the same GObj.
        Player_80031AD0(slot);
        runtime->fighters[i] = Player_GetEntityAtIndex(slot, 0);
        if (runtime->fighters[i] == NULL) {
            fprintf(stderr, "source Fighter_Create returned NULL for slot %d\n",
                    i);
            return -1;
        }
        seed_previous_input(GET_FIGHTER(runtime->fighters[i]),
                            &previous_input->p[i]);
        inject_pad_status(slot, &previous_input->p[i]);
        msl_ucf_seed_pad(slot, previous_input->p[i].main_x,
                         previous_input->p[i].main_y,
                         previous_input->p[i].c_x,
                         previous_input->p[i].c_y);
    }

    Camera_80030730(Ground_801C20D0());
    Ground_EnableMatchCamera();
    Camera_8002F3AC();
    // Slippi installs the versus on-unpause override. fn_8016E730 invokes it
    // during scene bootstrap and then snaps the standard camera a second
    // time; the second evaluation uses the depth established by the first.
    // refs/melee/src/melee/gm/gm_16AE.c::fn_8016E730
    // refs/slippi-ssbm-asm/External/OnFrame.asm::OnGameFirstFrame
    Camera_8002F3AC();

    // MslCoreMatchConfig names this as the seed immediately before the first
    // simulated frame, so constructor-time random choices do not consume it.
    seed = runtime->config.frame_pre_random_seed;
    seed_ptr = &seed;
    return 0;
}

static void put_player_u16(uint8_t* out, size_t field, int player,
                           uint16_t value)
{
    msl_core_put_le16(out + field + (size_t) player * sizeof(uint16_t), value);
}

static void put_player_u32(uint8_t* out, size_t field, int player,
                           uint32_t value)
{
    msl_core_put_le32(out + field + (size_t) player * sizeof(uint32_t), value);
}

static void put_player_f32(uint8_t* out, size_t field, int player, float value)
{
    msl_core_put_lef32(out + field + (size_t) player * sizeof(float), value);
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

static void write_item_compare(uint8_t* out, int slot, Item_GObj* gobj)
{
    Item* item = GET_ITEM(gobj);
    uint8_t* item_out =
        out + offsetof(MslCoreCompare, items) +
        (size_t) slot * sizeof(MslCoreItem);
    uint8_t* item_bytes = (uint8_t*) item;
    int8_t owner = -1;

    // Recording/SendItemInfo.s follows the owner GObj and reads the player
    // slot from user-data byte 0xC. Fox articles retain their fighter owner for
    // their complete lifetime, so the same source layout applies here.
    if (item->owner != NULL && item->owner->user_data != NULL) {
        owner = ((int8_t*) item->owner->user_data)[0xC];
    }

    item_out[offsetof(MslCoreItem, exists)] = 1;
    item_out[offsetof(MslCoreItem, state)] = (uint8_t) item->msid;
    msl_core_put_le16(item_out + offsetof(MslCoreItem, type),
                    (uint16_t) item->kind);
    item_out[offsetof(MslCoreItem, owner)] = (uint8_t) owner;
    msl_core_put_le16(item_out + offsetof(MslCoreItem, instance_id),
                    item->xDA8_short);
    msl_core_put_le16(item_out + offsetof(MslCoreItem, attack_id),
                    (uint16_t) item->xD88_attackID);
    msl_core_put_le16(item_out + offsetof(MslCoreItem, attack_instance),
                    item->xD8C_attack_instance);
    msl_core_put_lef32(item_out + offsetof(MslCoreItem, direction),
                     item->facing_dir);
    msl_core_put_lef32(item_out + offsetof(MslCoreItem, vel_x), item->x40_vel.x);
    msl_core_put_lef32(item_out + offsetof(MslCoreItem, vel_y), item->x40_vel.y);
    msl_core_put_lef32(item_out + offsetof(MslCoreItem, pos_x), item->pos.x);
    msl_core_put_lef32(item_out + offsetof(MslCoreItem, pos_y), item->pos.y);
    msl_core_put_le16(item_out + offsetof(MslCoreItem, damage),
                    (uint16_t) item->xC9C);
    msl_core_put_lef32(item_out + offsetof(MslCoreItem, timer),
                     item->xD44_lifeTimer);
    msl_core_put_le32(item_out + offsetof(MslCoreItem, spawn_id),
                    (uint32_t) item->x1C);
    // These four bytes are the exact metadata lanes exported by Slippi.
    // refs/slippi-ssbm-asm/Recording/SendItemInfo.s
    item_out[offsetof(MslCoreItem, misc0)] = item_bytes[0xDD7];
    item_out[offsetof(MslCoreItem, misc1)] = item_bytes[0xDDB];
    item_out[offsetof(MslCoreItem, misc2)] = item_bytes[0xDEB];
    item_out[offsetof(MslCoreItem, misc3)] = item_bytes[0xDEF];
}

static void write_compare(const MslCoreRuntime* runtime, uint32_t frame_seed,
                          MslCoreCompare* compare)
{
    uint8_t* out = (uint8_t*) compare;
    int i;

    memset(compare, 0, sizeof(*compare));
    msl_core_put_le32(out + offsetof(MslCoreCompare, frame_id),
                    (uint32_t) runtime->frame_id);
    msl_core_put_le32(out + offsetof(MslCoreCompare, frame_pre_random_seed),
                    frame_seed);
    msl_core_put_le32(out + offsetof(MslCoreCompare, stage_id),
                    MSL_CORE_STAGE_FINAL_DESTINATION);
    out[offsetof(MslCoreCompare, num_players)] = 2;
    out[offsetof(MslCoreCompare, is_teams)] = runtime->config.is_teams ? 1 : 0;
    // MslCoreCompare represents all four controller slots. Slots without a source
    // Fighter GObj use the validation contract's inactive/dead value.
    out[offsetof(MslCoreCompare, is_dead) + 2] = 1;
    out[offsetof(MslCoreCompare, is_dead) + 3] = 1;

    for (i = 0; i < 2; ++i) {
        Fighter* fp = GET_FIGHTER(runtime->fighters[i]);
        uint8_t* fighter_bytes = (uint8_t*) fp;
        float hitstun =
            (fighter_bytes[0x221C] & 0x02) ? fp->mv.co.damage.x0 : 0.0F;
        int hurtbox = fp->x1988 != 0 ? fp->x1988 : fp->x198C;
        int jumps_left = fp->co_attrs.max_jumps - fp->x1968_jumpsUsed;

        out[offsetof(MslCoreCompare, team_id) + i] = fp->team;
        out[offsetof(MslCoreCompare, char_id) + i] = MSL_CORE_CHAR_FOX;
        put_player_f32(out, offsetof(MslCoreCompare, pos_x), i, fp->cur_pos.x);
        put_player_f32(out, offsetof(MslCoreCompare, pos_y), i, fp->cur_pos.y);
        put_player_f32(out, offsetof(MslCoreCompare, speed_air_x_self), i,
                       fp->self_vel.x);
        put_player_f32(out, offsetof(MslCoreCompare, speed_ground_x_self), i,
                       fp->gr_vel);
        put_player_f32(out, offsetof(MslCoreCompare, speed_y_self), i,
                       fp->self_vel.y);
        put_player_f32(out, offsetof(MslCoreCompare, speed_x_attack), i,
                       fp->x8c_kb_vel.x);
        put_player_f32(out, offsetof(MslCoreCompare, speed_y_attack), i,
                       fp->x8c_kb_vel.y);
        out[offsetof(MslCoreCompare, facing) + i] = fp->facing_dir > 0.0F;
        out[offsetof(MslCoreCompare, on_ground) + i] =
            fp->ground_or_air == GA_Ground;
        // The existing MslCoreCompare contract uses this lane for eliminated
        // player slots, not the source Fighter's transient Dead motion flag.
        // Slippi-visible stock ownership remains Player_GetStocks.
        out[offsetof(MslCoreCompare, is_dead) + i] =
            Player_GetStocks(fp->player_id) == 0;
        put_player_u16(out, offsetof(MslCoreCompare, action_id), i,
                       (uint16_t) fp->motion_id);
        put_player_u16(out, offsetof(MslCoreCompare, action_frame), i,
                       (uint16_t) state_age_i16(fp->cur_anim_frame));
        out[offsetof(MslCoreCompare, jumps_left) + i] =
            jumps_left > 0 ? (uint8_t) jumps_left : 0;
        out[offsetof(MslCoreCompare, stocks) + i] =
            (uint8_t) Player_GetStocks(fp->player_id);
        put_player_f32(out, offsetof(MslCoreCompare, percent), i,
                       fp->dmg.x1830_percent);
        put_player_f32(out, offsetof(MslCoreCompare, shield_hp), i,
                       fp->shield_health);
        put_player_u16(out, offsetof(MslCoreCompare, hitlag), i,
                       float_frames_u16(fp->dmg.x195c_hitlag_frames));
        put_player_u16(out, offsetof(MslCoreCompare, hitstun), i,
                       float_frames_u16(hitstun));
        // Slippi's ExtendPlayerBlock/GetLCancelStatus patches own this byte.
        // refs/slippi-ssbm-asm/Recording/{Recording.s,GetLCancelStatus/}
        out[offsetof(MslCoreCompare, l_cancel) + i] = fighter_bytes[0x25FF];
        out[offsetof(MslCoreCompare, hurtbox_state) + i] = (uint8_t) hurtbox;
        put_player_u16(out, offsetof(MslCoreCompare, ground_id), i,
                       (uint16_t) fp->coll_data.floor.index);
        put_player_u32(out, offsetof(MslCoreCompare, animation_index), i,
                       (uint32_t) fp->anim_id);
        put_player_u16(out, offsetof(MslCoreCompare, instance_hit_by), i,
                       fp->dmg.x18ec_instancehitby);
        put_player_u16(out, offsetof(MslCoreCompare, instance_id), i,
                       fp->x2074.x2088);
        out[offsetof(MslCoreCompare, last_attack_landed) + i] =
            (uint8_t) fp->x208C;
        out[offsetof(MslCoreCompare, combo_count) + i] = (uint8_t) fp->x2090;
        out[offsetof(MslCoreCompare, last_hit_by) + i] =
            (uint8_t) fp->dmg.x18c4_source_ply;
        out[offsetof(MslCoreCompare, state_flags) + i * 5 + 0] =
            fighter_bytes[0x2218];
        out[offsetof(MslCoreCompare, state_flags) + i * 5 + 1] =
            fighter_bytes[0x221A];
        out[offsetof(MslCoreCompare, state_flags) + i * 5 + 2] =
            fighter_bytes[0x221B];
        out[offsetof(MslCoreCompare, state_flags) + i * 5 + 3] =
            fighter_bytes[0x221C];
        out[offsetof(MslCoreCompare, state_flags) + i * 5 + 4] =
            fighter_bytes[0x221F];
    }

    {
        Item_GObj* item_gobj = (Item_GObj*) HSD_GObj_Entities->items;
        int item_slot = 0;
        while (item_gobj != NULL && item_slot < MSL_CORE_MAX_ITEMS) {
            write_item_compare(out, item_slot++, item_gobj);
            item_gobj = (Item_GObj*) item_gobj->next;
        }
    }
}

static int runtime_step(MslCoreRuntime* runtime, const MslCoreInput* input,
                        uint32_t frame_seed, FILE* output)
{
    MslCoreCompare compare;
    int i;

    // Slippi's pre-frame row owns the RNG value used by replay playback.
    // Restore it once here, then let all source gameplay consumers advance the
    // ordinary HSD stream during this frame.
    // refs/slippi-ssbm-asm/{Recording/SendGamePreFrame.asm,
    // Playback/Core/RestoreGameFrame.asm}
    *seed_ptr = frame_seed;

    // The normal VS overlay is IfAll.dat::ScInfCnt_scene_models[3]. Its
    // completion callback, gm_16AE.c::fn_8016B7F8, calls ftLib_800868A4 before
    // raw frame -39 is processed. Frame -40 therefore remains locked and
    // preserves its physical stick in input.x630/x634; the first playable
    // frame inherits that sample as input history rather than seeing a fresh
    // dash flick. The renderer-owned overlay itself is absent headlessly.
    // refs/melee/src/melee/{gm/gm_16AE.c,if/ifstatus.c,if/if_2F72.c}
    // refs/melee-disc/files/IfAll.dat::ScInfCnt_scene_models[3]
    if (runtime->frame_id == -40) {
        ftLib_800868A4();
    }

    for (i = 0; i < 2; ++i) {
        int slot = runtime->source_slots[i];
        inject_pad_status(slot, &input->p[i]);
        msl_ucf_set_pending_pad(slot, input->p[i].main_x,
                                input->p[i].main_y, input->p[i].c_x,
                                input->p[i].c_y);
    }
    HSD_GObj_80390CFC();
    // Slippi's post-frame recorder snapshots fighter state after gameplay
    // processes but before the render pass. x221F_b0 therefore reflects the
    // preceding render in the exported row.
    // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    runtime->frame_id += 1;
    write_compare(runtime, frame_seed, &compare);

    // The subsequent retail render pass invokes ftDrawCommon_80080E18.
    // Preserve its camera-visibility publication for the next gameplay/
    // recording frame without retaining drawing. This is the canonical
    // headless schedule: one render publication per public sim step.
    //
    // Retail gm_801A4D34 can drain more than one queued pad sample through
    // HSD_GObj_80390CFC before a single HSD_GObj_80390FC0 render. Standard
    // Slippi files do not record that pad-queue/render boundary (and console
    // polling-drift codes change it), so reproducing an occasional stale
    // presentation bit would require an external scheduler signal rather than
    // a gameplay-state heuristic.
    // refs/melee/src/melee/gm/gm_1A45.c::gm_801A4D34
    // refs/melee/src/melee/lb/lb_0195.c::lb_80019894
    // refs/slippi-ssbm-asm/console_lag_pd*.json
    // refs/melee/src/melee/ft/ftdrawcommon.c::ftDrawCommon_80080E18
    for (i = 0; i < 2; ++i) {
        msl_camera_publish_fighter_visibility(runtime->fighters[i]);
    }
    // gm_8016AEDC is observed by fighter processes during this pass. The
    // source match owner advances it only after those processes have run.
    msl_core_advance_match_frame();
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
    uint8_t config_wire[sizeof(MslCoreMatchConfig)];
    MslCoreInput previous_input;
    MslCoreStreamFrame frame;
    MslCoreRuntime runtime;
    size_t count;

    if (fread(config_wire, 1, sizeof(config_wire), stdin) !=
            sizeof(config_wire) ||
        fread(&previous_input, 1, sizeof(previous_input), stdin) !=
            sizeof(previous_input)) {
        fprintf(stderr, "short Melee core stream header\n");
        return 1;
    }
    if (runtime_init(&runtime, data_root, config_wire, &previous_input) != 0) {
        return 1;
    }
    while ((count = fread(&frame, 1, sizeof(frame), stdin)) == sizeof(frame)) {
        uint32_t frame_seed = msl_core_get_le32(&frame.frame_pre_random_seed);
        if (runtime_step(&runtime, &frame.input, frame_seed, stdout) != 0) {
            return 1;
        }
    }
    if (count != 0 || ferror(stdin)) {
        fprintf(stderr, "short Melee core input stream\n");
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
    uint8_t config_wire[sizeof(MslCoreMatchConfig)];
    MslCoreInput previous_input;
    MslCoreInput input;
    MslCoreRuntime runtime;
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
        if (runtime_step(&runtime, &input, *seed_ptr, output_file) != 0) {
            goto done;
        }
    }
    if (count != 0 || ferror(input_file)) {
        fprintf(stderr, "%s is not a whole-number sequence of MslCoreInput rows\n",
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
