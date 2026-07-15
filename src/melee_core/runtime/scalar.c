#include "runtime/scalar.h"

#include "platform/files.h"
#ifdef MSL_CORE_NATIVE
#include "platform/native_dat.h"
#include "platform/memory.h"
#endif
#include "platform/slippi.h"
#include "runtime/match.h"
#include "runtime/effects.h"
#include "runtime/final_destination.h"
#include "runtime/wire.h"

#include "cm/camera.h"
#include "ft/fighter.h"
#include "ft/ftdevice.h"
#include "ft/ftdata.h"
#include "ft/ftlib.h"
#include "gr/grdatfiles.h"
#include "gr/grizumi.h"
#include "gr/ground.h"
#include "gr/types.h"
#include "it/inlines.h"
#include "it/it_26B1.h"
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
#include <baselib/class.h>
#include <baselib/controller.h>
#include <baselib/fobj.h>
#include <baselib/gobj.h>
#include <baselib/gobjuserdata.h>
#include <baselib/id.h>
#include <baselib/jobj.h>
#include <baselib/list.h>
#include <baselib/mtx.h>
#include <baselib/robj.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    MSL_CORE_STAGE_FOUNTAIN_OF_DREAMS = 2,
    MSL_CORE_STAGE_POKEMON_STADIUM = 3,
    MSL_CORE_STAGE_YOSHIS_STORY = 8,
    MSL_CORE_STAGE_DREAM_LAND = 28,
    MSL_CORE_STAGE_BATTLEFIELD = 31,
    MSL_CORE_STAGE_FINAL_DESTINATION = 32,
    MSL_CORE_CHAR_FOX = 1,
    MSL_CORE_CHAR_FALCO = 22,
    MSL_CORE_STICK_SCALE = 80,
};

extern u32 seed;
extern u32* seed_ptr;
extern int mpColl_804D64AC;
extern void Camera_8002B3D4(void* arg0);
extern StageData grIz_803E0E5C;
extern StageData grNBa_803E7E38;
extern StageData grOp_803E6748;
extern StageData grPs_803E1334;
extern StageData grSt_803E274C;

typedef struct MslCoreStageSpec {
    uint8_t external_id;
    InternalStageId internal_id;
    const char* archive;
    StageData* source;
    Vec3 spawn[2];
} MslCoreStageSpec;

// External list ids and singles spawn coordinates are the source-backed
// Slippi neutral-spawn contract. Archive/internal ids and stage entrypoints
// are the corresponding Melee source owners.
// refs/slippi-ssbm-asm/External/NeutralSpawn/NeutralSpawn.asm
// refs/melee/src/melee/gr/{grbattle.c,grpstadium.c,ground.c}
static const MslCoreStageSpec stage_specs[] = {
    { MSL_CORE_STAGE_FOUNTAIN_OF_DREAMS,
      IZUMI,
      "/GrIz.dat",
      &grIz_803E0E5C,
      { { -41.25F, 21.0F, 0.0F }, { 41.25F, 27.0F, 0.0F } } },
    { MSL_CORE_STAGE_POKEMON_STADIUM,
      PSTADIUM,
      "/GrPs.dat",
      &grPs_803E1334,
      { { -40.0F, 32.0F, 0.0F }, { 40.0F, 32.0F, 0.0F } } },
    { MSL_CORE_STAGE_YOSHIS_STORY,
      STORY,
      "/GrSt.dat",
      &grSt_803E274C,
      { { -42.0F, 26.6F, 0.0F }, { 42.0F, 28.0F, 0.0F } } },
    { MSL_CORE_STAGE_DREAM_LAND,
      OLDPUPUPU,
      "/GrOp.dat",
      &grOp_803E6748,
      { { -46.6F, 37.2F, 0.0F }, { 47.4F, 37.3F, 0.0F } } },
    { MSL_CORE_STAGE_BATTLEFIELD,
      BATTLE,
      "/GrNBa.dat",
      &grNBa_803E7E38,
      { { -38.8F, 35.2F, 0.0F }, { 38.8F, 35.2F, 0.0F } } },
    { MSL_CORE_STAGE_FINAL_DESTINATION,
      LAST,
      "/GrNLa.dat",
      NULL,
      { { -60.0F, 10.0F, 0.0F }, { 60.0F, 10.0F, 0.0F } } },
};

// grlast.c::grNLa_803E7F90 carries one collision-joint remap outside the DAT.
// Keep that exact gameplay metadata available to shared Ground helpers while
// retaining the already-validated manual FD object construction path.
static S16Vec3 fd_stage_collision_remap = { 0, 3, 0 };
static StageData fd_stage_data = {
    .internal_stage_id = LAST,
    .x2C = &fd_stage_collision_remap,
    .x30 = 1,
};

static MslCoreMatch* bound_stage_match;

static const MslCoreStageSpec* stage_spec(uint8_t external_id)
{
    size_t i;

    for (i = 0; i < sizeof(stage_specs) / sizeof(stage_specs[0]); ++i) {
        if (stage_specs[i].external_id == external_id) {
            return &stage_specs[i];
        }
    }
    return NULL;
}

static CharacterKind source_character_kind(uint8_t external_id)
{
    return external_id == MSL_CORE_CHAR_FALCO ? CKIND_FALCO : CKIND_FOX;
}

static void bind_stage_match(MslCoreMatch* match)
{
    bound_stage_match = match;
}

// Ground_GetStageGObj is source-shaped but its retail allocator is a global
// HSD heap. The hosted patch redirects only ownership: Ground bytes live in
// the selected Match, while the source stage callbacks remain unchanged.
// refs/melee/src/melee/gr/ground.c::Ground_GetStageGObj
Ground* msl_core_stage_ground_alloc(void)
{
    int i;

    for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
        if (!bound_stage_match->stage_ground_used[i]) {
            bound_stage_match->stage_ground_used[i] = 1;
            memset(&bound_stage_match->stage_ground[i], 0,
                   sizeof(bound_stage_match->stage_ground[i]));
            return &bound_stage_match->stage_ground[i];
        }
    }
    return NULL;
}

void msl_core_stage_ground_free(void* ground)
{
    int i;

    for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
        if (ground == &bound_stage_match->stage_ground[i]) {
            bound_stage_match->stage_ground_used[i] = 0;
            return;
        }
    }
}

StageCallbacks* msl_core_stage_callbacks(void)
{
    return bound_stage_match->stage_data->callbacks;
}

StageData* msl_core_selected_stage_data(void)
{
    return bound_stage_match->stage_data;
}

// Standard versus is never one of the three single-player modes selected by
// gm_8016B3D8, and Stage_80225194 publishes the configured external list id.
// refs/melee/src/melee/{gm/gm_16AE.c::gm_8016B3D8,
// gr/stage.c::Stage_80225194}
bool gm_8016B3D8(void)
{
    return false;
}

enum_t Stage_80225194(void)
{
    return bound_stage_match->config.stage_id;
}

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

static int validate_config(MslCoreMatchConfig* config)
{
    int i;

    if (stage_spec(config->stage_id) == NULL) {
        fprintf(stderr,
                "current core supports the six legal stage ids "
                "2,3,8,28,31,32\n");
        return -1;
    }
    if (config->num_players != 0 && config->num_players != 2) {
        fprintf(stderr,
                "current core requires num_players=2 (or zero default)\n");
        return -1;
    }
    for (i = 0; i < 2; ++i) {
        if (config->players[i].char_id != MSL_CORE_CHAR_FOX &&
            config->players[i].char_id != MSL_CORE_CHAR_FALCO) {
            fprintf(stderr,
                    "current core supports external char_id=1 Fox and "
                    "char_id=22 Falco only\n");
            return -1;
        }
    }
    config->num_players = 2;
    if (config->stock_count == 0) {
        config->stock_count = 4;
    }
    return 0;
}

int msl_core_game_data_init(MslCoreGameData* game_data, const char* data_root)
{
    size_t length;

    if (game_data == NULL || data_root == NULL || data_root[0] == '\0') {
        fprintf(stderr, "Melee core game-data root must not be empty\n");
        return -1;
    }
    length = strlen(data_root);
    if (length >= sizeof(game_data->root)) {
        fprintf(stderr, "Melee core game-data root is too long\n");
        return -1;
    }
    memset(game_data, 0, sizeof(*game_data));
    memcpy(game_data->root, data_root, length + 1);
    msl_host_set_data_root(game_data->root);
    init_hsd();
    msl_effect_game_data_init(&game_data->effects);
    return 0;
}

int msl_core_match_init(MslCoreMatch* match, const MslCoreGameData* game_data,
                        const MslCoreMatchConfig* config,
                        const MslCoreInput* previous_input)
{
    const MslCoreStageSpec* spec;
    UnkArchiveStruct* map_data;
    int i;

    if (match == NULL || game_data == NULL || config == NULL ||
        previous_input == NULL) {
        fprintf(stderr, "Melee core scalar init received a null owner\n");
        return -1;
    }
    memset(match, 0, sizeof(*match));
    match->game_data = game_data;
    match->config = *config;
    if (validate_config(&match->config) != 0) {
        return -1;
    }
    spec = stage_spec(match->config.stage_id);
    match->stage_data = spec->source != NULL ? spec->source : &fd_stage_data;
    bind_stage_match(match);
    match->frame_id = match->config.frame_id;
    for (i = 0; i < 2; ++i) {
        uint8_t encoded = match->config.players[i].facing_and_port;
        uint8_t port = encoded >> 1;
        match->source_slots[i] = port == 0 ? (uint8_t) i : (uint8_t) (port - 1);
        if (match->source_slots[i] >= MSL_CORE_MAX_PLAYERS ||
            (i != 0 && match->source_slots[i] == match->source_slots[0])) {
            fprintf(stderr, "invalid two-player physical port mapping\n");
            return -1;
        }
    }
    msl_core_match_rules_init(&match->rules, match->config.is_teams,
                              match->config.match_damage_ratio,
                              match->config.online_fnmsubs_zero,
                              match->config.brawl_offscreen_damage,
                              match->config.freeze_dead_up_fall_physics);
    msl_camera_state_init(&match->camera);
    msl_slippi_state_init(&match->slippi, match->config.stage_event_streams);

    msl_host_set_data_root(game_data->root);
    msl_effect_match_init(&game_data->effects, &match->effects);
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
        HSD_GObj_SetupProc(camera_gobj, (void (*)(HSD_GObj*)) Camera_8002B3D4,
                           0x12);
    }
    // Source stage-data bootstrap. The established FD projection stays intact;
    // Battlefield and Stadium enter through their imported StageData owners,
    // with only Ground storage redirected into this Match.
    // refs/melee/src/melee/gr/ground.c::{Ground_801C0754,Ground_801C0800}
    // refs/melee/src/melee/gm/gm_16AE.c::fn_8016E730
    Ground_801C0378(0x40);
    Ground_801BFFB0();
    stage_info.internal_stage_id = spec->internal_id;
    grDatFiles_801C6038((void*) spec->archive, 0, 0);
    if (stage_info.coll_data == NULL || stage_info.param == NULL) {
        fprintf(stderr, "failed to load stage data for external id %u\n",
                spec->external_id);
        return -1;
    }
    // Stage_802251E8 keeps the external stage-list id separately from the
    // internal stage kind; grGroundParam is keyed by that external list id.
    Ground_801C28CC(&stage_info.xA0, spec->external_id);
    if (spec->source != NULL) {
        stage_info.x178 = spec->source->callback5;
        stage_info.x17C = spec->source->callback6;
    }

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

    // Register stage-owned articles before OnInit can spawn them. This is the
    // gameplay-bearing itemdata slice of the common source stage bootstrap;
    // its adjacent fog/light/particle-bank setup remains headless.
    // refs/melee/src/melee/gr/ground.c::Ground_801C0800
    if (stage_info.itemdata != NULL) {
        for (i = 0; stage_info.itemdata[i] != NULL; ++i) {
            it_8026B40C(stage_info.itemdata[i]->unk4,
                        stage_info.itemdata[i]->unk0);
        }
    }

    // The versus bootstrap clears fighter/device registrations immediately
    // before stage construction. Stage OnInit callbacks such as Dream Land's
    // Whispy then register their source wind owner into the clean table.
    // refs/melee/src/melee/gm/gm_16AE.c::fn_8016F088
    // refs/melee/src/melee/ft/ftdevice.c::ftCo_800C06C0
    ftCo_800C06C0();
    mpColl_80041C78();
    mpLibLoad(stage_info.coll_data);
    mpLib_80058820();
    // Playback/Core/RestoreGameInfo.asm restores the game-start RNG before
    // stage construction; imported stage OnInit callbacks consume that stream
    // normally. Online frame seeds carry a later per-frame high-word offset
    // and are restored only by msl_core_match_step.
    // refs/slippi-ssbm-asm/{Recording/SendGameInfo.asm,
    // Playback/Core/RestoreGameInfo.asm,Online/Core/InitOnlinePlay.asm}
    seed = match->config.initial_random_seed;
    seed_ptr = &seed;
    if (spec->source != NULL) {
        // Fog/light/trophy-display setup in Ground_801C0800 is renderer-owned.
        // OnLoad is empty for both supported stage owners but remains invoked
        // to preserve the source lifecycle seam.
        spec->source->OnInit();
        spec->source->OnLoad();
    } else {
        map_data = grDatFiles_801C6324();
        if (map_data == NULL || map_data->unk4 == NULL) {
            fprintf(stderr, "Final Destination map data is missing\n");
            return -1;
        }
        if (map_data->unk4->unkC != MSL_CORE_STAGE_GROUND_CAPACITY) {
            fprintf(stderr, "unexpected Final Destination map GObj count %d\n",
                    map_data->unk4->unkC);
            return -1;
        }
        for (i = 0; i < map_data->unk4->unkC; ++i) {
        HSD_GObj* gobj;
        HSD_JObj* root;
        Ground* gp = &match->stage_ground[i];
        map_data = grDatFiles_801C6330(i);
        if (map_data == NULL || map_data->unk4 == NULL ||
            i >= map_data->unk4->unkC ||
            map_data->unk4->unk8[i].unk0 == NULL) {
            fprintf(stderr, "Final Destination map joint %d is missing\n", i);
            return -1;
        }
        root = HSD_JObjLoadJoint(map_data->unk4->unk8[i].unk0);
        if (root == NULL) {
            fprintf(stderr, "failed to load Final Destination map joint %d\n",
                    i);
            return -1;
        }
        Ground_801C34AC(i, root, map_data->unk4->unk8[i].unk0);
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
        match->stage_ground_used[i] = 1;
    }
        // grlast.c::grLast_OnInit gameplay-visible stage publication.
        stage_info.unk8C.b4 = true;
        stage_info.unk8C.b5 = true;
        Ground_801C39C0();
        Ground_801C3BB4();
    }
    // refs/melee/src/melee/gm/gm_16AE.c::fn_8016DCC0 and fn_8016E730.
    // Keep the source owners intact: Player_InitAllPlayers also initializes
    // each slot's statistics state, and Player_80036DD8 loads the common
    // player table consumed by the scheduled statistics pass.
    Player_InitAllPlayers();
    Player_80036DD8();
    for (i = 0; i < 2; ++i) {
        int slot = match->source_slots[i];
        uint8_t encoded = match->config.players[i].facing_and_port;
        float facing = (encoded >> 1) == 0 ? (i == 0 ? 1.0F : -1.0F)
                                           : ((encoded & 1) ? 1.0F : -1.0F);
        Player_SetPlayerCharacter(
            slot, source_character_kind(match->config.players[i].char_id));
        Player_SetSlottype(slot, Gm_PKind_Human);
        Player_SetTeam(slot, match->config.players[i].team_id);
        Player_SetStocks(slot, match->config.stock_count);
        Player_SetCostumeId(slot, match->config.players[i].costume_id);
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
        Player_80032768(slot, (Vec3*) &spec->spawn[i]);
    }
    // The versus bootstrap initializes fighter/device/item allocation before
    // Fighter_Create. Items are disabled in the current Fox/FD domain, so the
    // exact Item_80266FA8(false) entry is equivalent to Item_80266F70's source
    // gate without retaining the scene-owned item-switch aggregate.
    Item_80266FA8();
    Item_80266FCC();
    Player_80036DA4();
    for (i = 0; i < 2; ++i) {
        int slot = match->source_slots[i];
        // gm_16AE.c::fn_8016E2BC creates match fighters through the player
        // owner so player_entity/transformation state and scheduled player
        // bookkeeping refer to the same GObj.
        Player_80031AD0(slot);
        match->fighters[i] = Player_GetEntityAtIndex(slot, 0);
        if (match->fighters[i] == NULL) {
            fprintf(stderr, "source Fighter_Create returned NULL for slot %d\n",
                    i);
            return -1;
        }
        seed_previous_input(GET_FIGHTER(match->fighters[i]),
                            &previous_input->p[i]);
        inject_pad_status(slot, &previous_input->p[i]);
        msl_ucf_seed_pad(slot, previous_input->p[i].main_x,
                         previous_input->p[i].main_y, previous_input->p[i].c_x,
                         previous_input->p[i].c_y);
    }

#ifdef MSL_CORE_NATIVE
    for (i = 0; i < 2; ++i) {
        Fighter* fp = GET_FIGHTER(match->fighters[i]);
        FigaTree* active_tree = fp->x590;
        void* active_archive = fp->x5A4;
        int msid;

        // Native DAT graphs widen pointers once during match initialization.
        // Preload each fighter's complete action archive owner so frame-step
        // motion changes are allocation-free cache lookups.
        // refs/melee/src/melee/ft/ftdata.c::{ftData_80085A14,ftData_80085CD8}
        for (msid = 0; msid < fp->x58C; ++msid) {
            if (ftData_80085FD4(fp, msid)->x14 != 0) {
                ftData_80085CD8(fp, fp, msid);
            }
        }
        fp->x590 = active_tree;
        fp->x5A4 = active_archive;
    }
#endif

    Camera_80030730(Ground_801C20D0());
    Ground_EnableMatchCamera();
    Camera_8002F3AC();
    // Slippi installs the versus on-unpause override. fn_8016E730 invokes it
    // during scene bootstrap and then snaps the standard camera a second
    // time; the second evaluation uses the depth established by the first.
    // refs/melee/src/melee/gm/gm_16AE.c::fn_8016E730
    // refs/slippi-ssbm-asm/External/OnFrame.asm::OnGameFirstFrame
    Camera_8002F3AC();

#ifdef MSL_CORE_NATIVE
    // Baselib's animation allocators grow one object at a time on demand.
    // Reserve the bounded two-fighter Phase 5 scalar domain before sealing the
    // HSD heap so later motion, article, stage, and quake objects only recycle
    // source free lists. Animation graphs have more nodes than the general
    // pools; Falco plus animated Battlefield/Stadium raises the reached FObj
    // high-water mark above the original Fox/FD reserve.
    // refs/melee/src/sysdolphin/baselib/{aobj.c,fobj.c,objalloc.c}
    // A five-Heiho Yoshi wave installs the source item process set in one
    // callback, raising the GObjProc free-list demand above the static-stage
    // bootstrap. Keep a fixed per-type reserve that covers the supported
    // stage-actor high-water mark without permitting runtime heap growth.
    HSD_ObjAllocPreallocateAll(256);
    HSD_ObjAllocAddFree(HSD_AObjGetAllocData(), 448);
    HSD_ObjAllocAddFree(HSD_FObjGetAllocData(), 1024);
    HSD_ObjAllocAddFree(HSD_IDGetAllocData(), 192);
    hsdPreallocateMemPieces(64);

    // All reached archive graphs and source allocation pools are complete.
    // Seal both boundaries before the first public frame; the full replay gate
    // then acts as a runtime proof that no lazy translation, raw game-file
    // access, or HSD heap growth remains.
    msl_native_dat_finish_initialization();
    msl_memory_finish_initialization();
#endif

    // MslCoreMatchConfig names this as the seed immediately before the first
    // simulated frame, so constructor-time random choices do not consume it.
    seed = match->config.frame_pre_random_seed;
    seed_ptr = &seed;
    match->random_seed = seed;
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

static uint8_t item_var_source_byte(const Item* item, size_t source_offset)
{
#ifdef MSL_CORE_NATIVE
    uint32_t word;
    size_t word_offset = source_offset & ~(size_t) 3;
    unsigned int shift = (unsigned int) (3 - (source_offset & 3)) * 8;

    // Slippi exports bytes from the retail big-endian item-variable union.
    // The reached Fox article variables are 32-bit scalar/vector lanes through
    // these offsets, so serialize the numeric source word in PPC byte order.
    // refs/melee/src/melee/it/{types.h,itCharItems.h}
    // refs/slippi-ssbm-asm/Recording/SendItemInfo.s
    memcpy(&word, (const uint8_t*) &item->xDD4_itemVar + word_offset,
           sizeof(word));
    return (uint8_t) (word >> shift);
#else
    return ((const uint8_t*) item)[0xDD4 + source_offset];
#endif
}

static void write_item_compare(uint8_t* out, int slot, Item_GObj* gobj)
{
    Item* item = GET_ITEM(gobj);
    uint8_t* item_out = out + offsetof(MslCoreCompare, items) +
                        (size_t) slot * sizeof(MslCoreItem);
    int8_t owner = -1;

    // Recording/SendItemInfo.s follows the owner GObj and reads the player
    // slot from user-data byte 0xC. Fox articles retain their fighter owner for
    // their complete lifetime, so the same source layout applies here.
    if (item->owner != NULL && item->owner->user_data != NULL) {
        owner = GET_FIGHTER(item->owner)->player_id;
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
    msl_core_put_lef32(item_out + offsetof(MslCoreItem, vel_x),
                       item->x40_vel.x);
    msl_core_put_lef32(item_out + offsetof(MslCoreItem, vel_y),
                       item->x40_vel.y);
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
    item_out[offsetof(MslCoreItem, misc0)] = item_var_source_byte(item, 3);
    item_out[offsetof(MslCoreItem, misc1)] = item_var_source_byte(item, 7);
    item_out[offsetof(MslCoreItem, misc2)] = item_var_source_byte(item, 0x17);
    item_out[offsetof(MslCoreItem, misc3)] = item_var_source_byte(item, 0x1B);
}

static uint8_t ppc_state_bit(unsigned int value, unsigned int index)
{
    return value != 0 ? (uint8_t) (0x80U >> index) : 0;
}

static void pack_fighter_state_flags(const Fighter* fp, uint8_t flags[5])
{
    flags[0] = ppc_state_bit(fp->allow_interrupt, 0) |
               ppc_state_bit(fp->x2218_b1, 1) | ppc_state_bit(fp->x2218_b2, 2) |
               ppc_state_bit(fp->reflecting, 3) |
               ppc_state_bit(fp->x2218_b4, 4) | ppc_state_bit(fp->x2218_b5, 5) |
               ppc_state_bit(fp->x2218_b6, 6) | ppc_state_bit(fp->x2218_b7, 7);
    flags[1] =
        ppc_state_bit(fp->x221A_b0, 0) | ppc_state_bit(fp->x221A_b1, 1) |
        ppc_state_bit(fp->allow_sdi, 2) | ppc_state_bit(fp->x221A_b3, 3) |
        ppc_state_bit(fp->fall_fast, 4) | ppc_state_bit(fp->x221A_b5, 5) |
        ppc_state_bit(fp->x221A_b6, 6) | ppc_state_bit(fp->x221A_b7, 7);
    flags[2] = ppc_state_bit(fp->x221B_b0, 0) | ppc_state_bit(fp->x221B_b1, 1) |
               ppc_state_bit(fp->x221B_b2, 2) | ppc_state_bit(fp->x221B_b3, 3) |
               ppc_state_bit(fp->x221B_b4, 4) | ppc_state_bit(fp->x221B_b5, 5) |
               ppc_state_bit(fp->x221B_b6, 6) | ppc_state_bit(fp->x221B_b7, 7);
    flags[3] = ppc_state_bit(fp->x221C_b0, 0) | ppc_state_bit(fp->x221C_b1, 1) |
               ppc_state_bit(fp->x221C_b2, 2) | ppc_state_bit(fp->x221C_b3, 3) |
               ppc_state_bit(fp->x221C_b4, 4) | ppc_state_bit(fp->x221C_b5, 5) |
               ppc_state_bit(fp->x221C_b6, 6) |
               ppc_state_bit(fp->x221C_u16_y & 4U, 7);
    flags[4] = ppc_state_bit(fp->x221F_b0, 0) | ppc_state_bit(fp->x221F_b1, 1) |
               ppc_state_bit(fp->x221F_b2, 2) | ppc_state_bit(fp->x221F_b3, 3) |
               ppc_state_bit(fp->x221F_b4, 4) | ppc_state_bit(fp->x221F_b5, 5) |
               ppc_state_bit(fp->x221F_b6, 6) | ppc_state_bit(fp->x221F_b7, 7);
}

static void write_compare(const MslCoreMatch* match, uint32_t frame_seed,
                          MslCoreCompare* compare)
{
    uint8_t* out = (uint8_t*) compare;
    int i;

    memset(compare, 0, sizeof(*compare));
    msl_core_put_le32(out + offsetof(MslCoreCompare, frame_id),
                      (uint32_t) match->frame_id);
    msl_core_put_le32(out + offsetof(MslCoreCompare, frame_pre_random_seed),
                      frame_seed);
    msl_core_put_le32(out + offsetof(MslCoreCompare, stage_id),
                      match->config.stage_id);
    out[offsetof(MslCoreCompare, num_players)] = 2;
    out[offsetof(MslCoreCompare, is_teams)] = match->config.is_teams ? 1 : 0;
    // MslCoreCompare represents all four controller slots. Slots without a
    // source Fighter GObj use the validation contract's inactive/dead value.
    out[offsetof(MslCoreCompare, is_dead) + 2] = 1;
    out[offsetof(MslCoreCompare, is_dead) + 3] = 1;

    for (i = 0; i < 2; ++i) {
        Fighter* fp = GET_FIGHTER(match->fighters[i]);
        uint8_t state_flags[5];
        float hitstun = fp->x221C_b6 ? fp->mv.co.damage.x0 : 0.0F;
        int hurtbox = fp->x1988 != 0 ? fp->x1988 : fp->x198C;
        int jumps_left = fp->co_attrs.max_jumps - fp->x1968_jumpsUsed;

        out[offsetof(MslCoreCompare, team_id) + i] = fp->team;
        out[offsetof(MslCoreCompare, char_id) + i] =
            match->config.players[i].char_id;
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
        out[offsetof(MslCoreCompare, l_cancel) + i] =
            msl_slippi_lcancel_get(fp);
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
        pack_fighter_state_flags(fp, state_flags);
        memcpy(out + offsetof(MslCoreCompare, state_flags) + i * 5, state_flags,
               sizeof(state_flags));
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

static void apply_replay_stage_events(MslCoreMatch* match)
{
    int i;
    if (match->config.stage_id == MSL_CORE_STAGE_FOUNTAIN_OF_DREAMS &&
        (match->config.stage_event_streams & 1) != 0)
    {
        for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
            Ground* gp = &match->stage_ground[i];
            f32 height;
            bool changed;
            if (!match->stage_ground_used[i] || gp->map_id != 4) {
                continue;
            }
            // grIzumi creates the left/right collision actors in the reverse
            // order of Slippi's 0=right, 1=left event protocol. Publish the
            // event through the source xD0/JObj/mpLib owner before fighter
            // collision runs, then the source callback observes the same
            // retained height later in the scheduler.
            // refs/slippi-ssbm-asm/Recording/Stages/SendFountainInfo.asm
            // refs/melee/src/melee/gr/grizumi.c::{
            //   grIzumi_801CC358,grIzumi_801CCBDC}
            if (msl_slippi_fod_platform_height(1 - gp->gv.izumi3.xC8,
                                               gp->gv.izumi3.xD0, &height,
                                               &changed))
            {
                msl_grizumi_apply_replay_platform_height(gp->gobj, height,
                                                          changed);
                msl_slippi_fod_platform_mark_applied(
                    1 - gp->gv.izumi3.xC8);
            }
        }
    } else if (match->config.stage_id == MSL_CORE_STAGE_DREAM_LAND &&
               (match->config.stage_event_streams & 2) != 0)
    {
        u8 direction;
        if (msl_slippi_dreamland_whispy_direction(&direction)) {
            for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
                Ground* gp = &match->stage_ground[i];
                if (match->stage_ground_used[i] && gp->map_id == 7) {
                    // The replay event is emitted when source xDC changes.
                    // Make it visible before the fighter/device pass even
                    // though Whispy's own stage process runs later.
                    // refs/melee/src/melee/gr/groldpupupu.c::{
                    //   grOldPupupu_8021119C,grOldPupupu_802113E0,
                    //   fn_802112F4}
                    gp->gv.oldpupupu.xDC = direction;
                    break;
                }
            }
        }
    }
}

static void publish_render_matrices(HSD_JObj* jobj)
{
    HSD_JObj* child;

    if (jobj == NULL) {
        return;
    }
    HSD_JObjSetupMatrix(jobj);
    if (jobj->flags & JOBJ_INSTANCE) {
        return;
    }
    for (child = jobj->child; child != NULL; child = child->next) {
        publish_render_matrices(child);
    }
}

int msl_core_match_step(MslCoreMatch* match, const MslCoreInput* input,
                        uint32_t frame_seed,
                        const MslCoreStageEvents* stage_events)
{
    int i;

    if (match == NULL || input == NULL || stage_events == NULL) {
        fprintf(stderr, "Melee core scalar step received a null owner\n");
        return -1;
    }
    msl_core_bind_match_rules(&match->rules);
    msl_camera_state_bind(&match->camera);
    msl_effect_projection_bind(&match->game_data->effects, &match->effects);
    msl_slippi_state_bind(&match->slippi);
    msl_slippi_stage_events_begin(stage_events);
    bind_stage_match(match);
    apply_replay_stage_events(match);

    // Slippi's pre-frame row owns the RNG value used by replay playback.
    // Restore it once here, then let all source gameplay consumers advance the
    // ordinary HSD stream during this frame.
    // refs/slippi-ssbm-asm/{Recording/SendGamePreFrame.asm,
    // Playback/Core/RestoreGameFrame.asm}
    *seed_ptr = frame_seed;
    match->random_seed = frame_seed;

    // The normal VS overlay is IfAll.dat::ScInfCnt_scene_models[3]. Its
    // completion callback, gm_16AE.c::fn_8016B7F8, calls ftLib_800868A4 before
    // raw frame -39 is processed. Frame -40 therefore remains locked and
    // preserves its physical stick in input.x630/x634; the first playable
    // frame inherits that sample as input history rather than seeing a fresh
    // dash flick. The renderer-owned overlay itself is absent headlessly.
    // refs/melee/src/melee/{gm/gm_16AE.c,if/ifstatus.c,if/if_2F72.c}
    // SSBM.iso::IfAll.dat::ScInfCnt_scene_models[3]
    if (match->frame_id == -40) {
        ftLib_800868A4();
    }

    for (i = 0; i < 2; ++i) {
        int slot = match->source_slots[i];
        inject_pad_status(slot, &input->p[i]);
        msl_ucf_set_pending_pad(slot, input->p[i].main_x, input->p[i].main_y,
                                input->p[i].c_x, input->p[i].c_y);
    }
    HSD_GObj_80390CFC();
    // Slippi's post-frame recorder snapshots fighter state after gameplay
    // processes but before the render pass. x221F_b0 therefore reflects the
    // preceding render in the exported row.
    // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    match->frame_id += 1;
    write_compare(match, frame_seed, &match->output);

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
        // Retail's intervening fighter draw walks the complete visible JObj
        // tree through HSD_JObjDispAll. The GX/DObj work is presentation, but
        // its ordered lazy-matrix publication is observed by next-frame
        // hit/hurt capsules, including RObj/IK-dependent bones. Preserve that
        // source boundary without invoking the renderer.
        // refs/melee/src/melee/ft/ftdrawcommon.c::{
        //   ftDrawCommon_80080E18,ftDrawCommon_800805C8}
        // refs/melee/src/sysdolphin/baselib/jobj.c::{
        //   HSD_JObjDispAll,HSD_JObjSetupMatrixSub}
        publish_render_matrices(match->fighters[i]->hsd_obj);
        msl_camera_publish_fighter_visibility(match->fighters[i]);
    }
    {
        Item_GObj* item_gobj = (Item_GObj*) HSD_GObj_Entities->items;
        while (item_gobj != NULL) {
            // Item display callbacks likewise walk the gameplay JObj tree via
            // HSD_JObjDispAll. Preserve the lazy matrix publication while
            // omitting DObj/GX work; item hit/hurt capsules can observe it on
            // the following gameplay frame.
            // refs/melee/src/melee/it/itdraw.c::it_8026EB18
            publish_render_matrices(item_gobj->hsd_obj);
            item_gobj = (Item_GObj*) item_gobj->next;
        }
    }
    // gm_8016AEDC is observed by fighter processes during this pass. The
    // source match owner advances it only after those processes have run.
    msl_core_advance_match_frame();
    match->random_seed = *seed_ptr;
    return 0;
}

const MslCoreCompare* msl_core_match_output(const MslCoreMatch* match)
{
    return &match->output;
}
