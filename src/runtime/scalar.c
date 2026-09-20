#include "runtime/scalar.h"
#include "runtime/item_projection.h"

#include "platform/files.h"
#ifdef MSL_CORE_NATIVE
#include "platform/memory.h"
#include "platform/native_dat.h"
#endif
#include "cm/camera.h"
#include "ftKirby/ftkirby.h"
#include "ft/fighter.h"
#include "ft/ftdata.h"
#include "ft/ftdevice.h"
#include "ft/ftlib.h"
#include "gr/grdatfiles.h"
#include "gr/grizumi.h"
#include "gr/ground.h"
#include "gr/types.h"
#include "it/inlines.h"
#include "it/it_26B1.h"
#include "it/itCharItems.h"
#include "it/item.h"
#include "it/types.h"
#include "lb/lbarchive.h"
#include "lb/lbspdisplay.h"
#include "mp/mpcoll.h"
#include "mp/mplib.h"
#include "mp/types.h"
#include "pl/player.h"
#include "pl/types.h"
#include "platform/slippi.h"
#include "runtime/context.h"
#include "runtime/effects.h"
#include "runtime/match.h"
#include "runtime/subsystem_profile.h"
#include "runtime/wire.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <baselib/aobj.h>
#include <baselib/class.h>
#include <baselib/controller.h>
#include <baselib/fobj.h>
#include <baselib/gobj.h>
#include <baselib/gobjobject.h>
#include <baselib/gobjproc.h>
#include <baselib/gobjuserdata.h>
#include <baselib/id.h>
#include <baselib/jobj.h>
#include <baselib/list.h>
#include <baselib/mtx.h>
#include <baselib/memory.h>
#include <baselib/random.h>
#include <baselib/robj.h>
#include <MSL/math.h>

enum {
    MSL_CORE_STAGE_FOUNTAIN_OF_DREAMS = 2,
    MSL_CORE_STAGE_POKEMON_STADIUM = 3,
    MSL_CORE_STAGE_YOSHIS_STORY = 8,
    MSL_CORE_STAGE_DREAM_LAND = 28,
    MSL_CORE_STAGE_BATTLEFIELD = 31,
    MSL_CORE_STAGE_FINAL_DESTINATION = 32,
    MSL_CORE_CHAR_MARIO = 0,
    MSL_CORE_CHAR_FOX = 1,
    MSL_CORE_CHAR_CAPTAIN_FALCON = 2,
    MSL_CORE_CHAR_DONKEY = 3,
    MSL_CORE_CHAR_GANONDORF = 25,
    MSL_CHAR_YOSHI = 14,
    MSL_CHAR_BOWSER = 5,
    MSL_CORE_CHAR_SHEIK = 7,
    MSL_CORE_CHAR_PEACH = 9,
    MSL_CORE_CHAR_POPO = 10,
    MSL_CORE_CHAR_PIKACHU = 12,
    MSL_CORE_CHAR_KIRBY = 4,
    MSL_CORE_CHAR_SAMUS = 13,
    MSL_CHAR_MEWTWO = 16,
    MSL_CHAR_GAMEWATCH = 24,
    MSL_CORE_CHAR_NESS = 8,
    MSL_CORE_CHAR_LINK = 6,
    MSL_CORE_CHAR_YOUNG_LINK = 20,
    MSL_CORE_CHAR_JIGGLYPUFF = 15,
    MSL_CORE_CHAR_LUIGI = 17,
    MSL_CORE_CHAR_DRMARIO = 21,
    MSL_CORE_CHAR_MARTH = 18,
    MSL_CORE_CHAR_ZELDA = 19,
    MSL_CORE_CHAR_FALCO = 22,
    MSL_CORE_STICK_SCALE = 80,
};

extern void Camera_8002B3D4(void* arg0);
extern StageData grIz_803E0E5C;
extern StageData grNBa_803E7E38;
extern StageData grNLa_803E7F90;
extern StageData grOp_803E6748;
extern StageData grPs_803E1334;
extern StageData grSt_803E274C;

typedef struct MslCoreStageSpec {
    uint8_t external_id;
    InternalStageId internal_id;
    const char* archive;
    StageData* source;
    uint16_t runtime_animation_mask;
    uint16_t static_stage_proc_mask;
    Vec3 singles_spawn[MSL_CORE_MAX_PLAYERS];
    Vec3 teams_spawn[MSL_CORE_MAX_PLAYERS];
} MslCoreStageSpec;

#ifdef MSL_CORE_NATIVE
static int preload_supported_game_data(MslCoreGameData* game_data);
#endif

static uint64_t fingerprint_game_data(const MslCoreGameData* game_data)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t i;
    for (i = 0; i < game_data->files.raw_count; ++i) {
        const MslRawFileEntry* entry = &game_data->files.raw[i];
        const uint8_t* cursor = (const uint8_t*) entry->basename;
        size_t remaining = strlen(entry->basename) + 1;
        while (remaining-- != 0) {
            hash = (hash ^ *cursor++) * UINT64_C(1099511628211);
        }
        cursor = (const uint8_t*) &entry->size;
        remaining = sizeof(entry->size);
        while (remaining-- != 0) {
            hash = (hash ^ *cursor++) * UINT64_C(1099511628211);
        }
        cursor = entry->data;
        remaining = entry->size;
        while (remaining-- != 0) {
            hash = (hash ^ *cursor++) * UINT64_C(1099511628211);
        }
    }
    return hash;
}

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
      1U << 4,
      0,
      { { -41.25F, 21.0F, 0.0F }, { 41.25F, 27.0F, 0.0F },
        { 0.0F, 5.25F, 0.0F }, { 0.0F, 48.0F, 0.0F } },
      { { -41.25F, 21.0F, 0.0F }, { -41.25F, 5.0F, 0.0F },
        { 41.25F, 27.0F, 0.0F }, { 41.25F, 5.0F, 0.0F } } },
    { MSL_CORE_STAGE_POKEMON_STADIUM,
      PSTADIUM,
      "/GrPs.dat",
      &grPs_803E1334,
      0,
      1U << 5,
      { { -40.0F, 32.0F, 0.0F }, { 40.0F, 32.0F, 0.0F },
        { 70.0F, 7.0F, 0.0F }, { -70.0F, 7.0F, 0.0F } },
      { { -40.0F, 32.0F, 0.0F }, { -40.0F, 5.0F, 0.0F },
        { 40.0F, 32.0F, 0.0F }, { 40.0F, 5.0F, 0.0F } } },
    { MSL_CORE_STAGE_YOSHIS_STORY,
      STORY,
      "/GrSt.dat",
      &grSt_803E274C,
      (1U << 2) | (1U << 3),
      0,
      { { -42.0F, 26.6F, 0.0F }, { 42.0F, 28.0F, 0.0F },
        { 0.0F, 46.9F, 0.0F }, { 0.0F, 4.9F, 0.0F } },
      { { -42.0F, 26.6F, 0.0F }, { -42.0F, 5.0F, 0.0F },
        { 42.0F, 28.0F, 0.0F }, { 42.0F, 5.0F, 0.0F } } },
    { MSL_CORE_STAGE_DREAM_LAND,
      OLDPUPUPU,
      "/GrOp.dat",
      &grOp_803E6748,
      (1U << 2) | (1U << 7),
      0,
      { { -46.6F, 37.2F, 0.0F }, { 47.4F, 37.3F, 0.0F },
        { 0.0F, 7.0F, 0.0F }, { 0.0F, 58.5F, 0.0F } },
      { { -46.6F, 37.2F, 0.0F }, { -46.6F, 5.0F, 0.0F },
        { 47.4F, 37.3F, 0.0F }, { 47.4F, 5.0F, 0.0F } } },
    { MSL_CORE_STAGE_BATTLEFIELD,
      BATTLE,
      "/GrNBa.dat",
      &grNBa_803E7E38,
      (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4),
      // Map 6's per-frame proc (grBattle_8021A174) is not render-only: it
      // advances the shared dynamics-force emitters (lb_800115F4) that gate
      // fighter tail gusts. Keep it scheduled.
      0,
      { { -38.8F, 35.2F, 0.0F }, { 38.8F, 35.2F, 0.0F },
        { 0.0F, 8.0F, 0.0F }, { 0.0F, 62.4F, 0.0F } },
      { { -38.8F, 35.2F, 0.0F }, { -38.8F, 5.0F, 0.0F },
        { 38.8F, 35.2F, 0.0F }, { 38.8F, 5.0F, 0.0F } } },
    { MSL_CORE_STAGE_FINAL_DESTINATION,
      LAST,
      "/GrNLa.dat",
      &grNLa_803E7F90,
      (1U << 3) | (1U << 4) | (1U << 5) | (1U << 6) |
          (1U << 7) | (1U << 8) | (1U << 9),
      0,
      { { -60.0F, 10.0F, 0.0F }, { 60.0F, 10.0F, 0.0F },
        { -20.0F, 10.0F, 0.0F }, { 20.0F, 10.0F, 0.0F } },
      { { -60.0F, 10.0F, 0.0F }, { -20.0F, 10.0F, 0.0F },
        { 60.0F, 10.0F, 0.0F }, { 20.0F, 10.0F, 0.0F } } },
};

static _Thread_local MslCoreMatch* bound_stage_match;

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

static int neutral_spawn_order(const MslCoreMatch* match, int player)
{
    int i;
    int order = 0;

    if (!match->config.is_teams) {
        for (i = 0; i < match->config.num_players; ++i) {
            if (match->source_slots[i] < match->source_slots[player]) {
                ++order;
            }
        }
        return order;
    }

    // NeutralSpawn.asm creates its 2v2 order by ascending team id and then
    // ascending physical player slot. Preserve that source table index even
    // when the validation/config lanes are not already in team order.
    // refs/slippi-ssbm-asm/External/NeutralSpawn/NeutralSpawn.asm::{
    //   CreateTeamArray,SearchForPlayerID,NeutralSpawnTable}
    for (i = 0; i < match->config.num_players; ++i) {
        uint8_t other_team = match->config.players[i].team_id;
        uint8_t player_team = match->config.players[player].team_id;
        if (other_team < player_team ||
            (other_team == player_team &&
             match->source_slots[i] < match->source_slots[player])) {
            ++order;
        }
    }
    return order;
}

static CharacterKind source_character_kind(uint8_t external_id)
{
    switch (external_id) {
    case MSL_CORE_CHAR_CAPTAIN_FALCON:
        return CKIND_CAPTAIN;
    case MSL_CORE_CHAR_SHEIK:
        return CKIND_SEAK;
    case MSL_CORE_CHAR_PEACH:
        return CKIND_PEACH;
    case MSL_CORE_CHAR_SAMUS:
        return CKIND_SAMUS;
    case MSL_CHAR_MEWTWO:
        return CKIND_MEWTWO;
    case MSL_CHAR_GAMEWATCH:
        return CKIND_GAMEWATCH;
    case MSL_CORE_CHAR_NESS:
        return CKIND_NESS;
    case MSL_CORE_CHAR_LINK:
        return CKIND_LINK;
    case MSL_CORE_CHAR_YOUNG_LINK:
        return CKIND_CLINK;
    case MSL_CORE_CHAR_POPO:
        return CKIND_POPONANA;
    case MSL_CORE_CHAR_DONKEY:
        return CKIND_DONKEY;
    case MSL_CORE_CHAR_GANONDORF:
        return CKIND_GANON;
    case MSL_CHAR_YOSHI:
        return CKIND_YOSHI;
    case MSL_CHAR_BOWSER:
        return CKIND_KOOPA;
    case MSL_CORE_CHAR_PIKACHU:
        return CKIND_PIKACHU;
    case MSL_CHARACTER_PICHU:
        return CKIND_PICHU;
    case MSL_CORE_CHAR_KIRBY:
        return CKIND_KIRBY;
    case MSL_CORE_CHAR_JIGGLYPUFF:
        return CKIND_PURIN;
    case MSL_CORE_CHAR_LUIGI:
        return CKIND_LUIGI;
    case MSL_CORE_CHAR_MARIO:
        return CKIND_MARIO;
    case MSL_CORE_CHAR_DRMARIO:
        return CKIND_DRMARIO;
    case MSL_CORE_CHAR_MARTH:
        return CKIND_MARS;
    case MSL_CHARACTER_ROY:
        return CKIND_EMBLEM;
    case MSL_CORE_CHAR_ZELDA:
        return CKIND_ZELDA;
    case MSL_CORE_CHAR_FALCO:
        return CKIND_FALCO;
    default:
        return CKIND_FOX;
    }
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

static void configure_headless_ground_schedule(
    MslCoreMatch* match, const MslCoreStageSpec* spec)
{
    int i;
    for (i = 0; i < MSL_CORE_STAGE_GROUND_CAPACITY; ++i) {
        Ground* gp;
        uint16_t map_bit;
        if (!match->stage_ground_used[i]) {
            continue;
        }
        gp = &match->stage_ground[i];
        HSD_ASSERT(501, gp->map_id >= 0 && gp->map_id < 16);
        map_bit = (uint16_t) (1U << gp->map_id);
        if ((spec->runtime_animation_mask & map_bit) == 0) {
            msl_ground_use_headless_epoch_proc(gp->gobj);
        }
        if ((spec->static_stage_proc_mask & map_bit) != 0) {
            msl_ground_remove_priority4_procs(gp->gobj);
        } else {
            msl_ground_remove_null_post_proc(gp->gobj);
        }
    }
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
    // GameData runs the shared class/catalog initialization above once too,
    // but has no mutable fighter-dynamics owner.
    if (msl_core_try_active_match() != NULL) {
        lb_8000FCDC();
    }
}

#ifdef MSL_CORE_NATIVE
static void init_relocation_types(void)
{
    // Each source allocator owns a fixed concrete object family. Record that
    // source type at pool construction so savestate relocation visits only
    // compiler-described pointer slots (plus the allocator's explicit first
    // word free-list overlay).
    HSD_ObjAllocSetRelocType(HSD_AObjGetAllocData(), MSL_RELOC_HSD_AOBJ, 1,
                             sizeof(HSD_AObj));
    HSD_ObjAllocSetRelocType(HSD_FObjGetAllocData(), MSL_RELOC_HSD_FOBJ, 1,
                             sizeof(HSD_FObj));
    HSD_ObjAllocSetRelocType(HSD_IDGetAllocData(), MSL_RELOC_ID_ENTRY, 1,
                             sizeof(IDEntry));
    HSD_ObjAllocSetRelocType(HSD_RObjGetAllocData(), MSL_RELOC_HSD_ROBJ, 1,
                             sizeof(HSD_RObj));
    HSD_ObjAllocSetRelocType(HSD_RvalueObjGetAllocData(), MSL_RELOC_HSD_RVALUE,
                             1, sizeof(HSD_Rvalue));
    HSD_ObjAllocSetRelocType(HSD_SListGetAllocData(), MSL_RELOC_HSD_SLIST, 1,
                             sizeof(HSD_SList));
    HSD_ObjAllocSetRelocType(HSD_DListGetAllocData(), MSL_RELOC_HSD_DLIST, 1,
                             sizeof(HSD_DList));
    HSD_ObjAllocSetRelocType(&gobj_alloc_data, MSL_RELOC_HSD_GOBJ, 1,
                             sizeof(HSD_GObj));
    HSD_ObjAllocSetRelocType(&gobjproc_alloc_data, MSL_RELOC_HSD_GOBJPROC, 1,
                             sizeof(HSD_GObjProc));
    HSD_ObjAllocSetRelocType(&fighter_alloc_data, MSL_RELOC_FIGHTER, 1,
                             sizeof(Fighter));
    HSD_ObjAllocSetRelocType(&fighter_dat_attrs_alloc_data, MSL_RELOC_RAW, 1,
                             0x424);
    HSD_ObjAllocSetRelocType(&fighter_parts_alloc_data, MSL_RELOC_FIGHTER_BONE,
                             FIGHTER_PARTS_ALLOC_COUNT, sizeof(FighterBone));
    HSD_ObjAllocSetRelocType(&fighter_dobj_list_alloc_data,
                             MSL_RELOC_POINTER_ARRAY, FIGHTER_DOBJ_ALLOC_COUNT,
                             sizeof(void*));
    HSD_ObjAllocSetRelocType(&fighter_x2040_alloc_data,
                             MSL_RELOC_POINTER_ARRAY,
                             FIGHTER_X2040_ALLOC_COUNT, sizeof(void*));
    HSD_ObjAllocSetRelocType(&fighter_x59C_alloc_data, MSL_RELOC_RAW, 1,
                             0x8000);
    HSD_ObjAllocSetRelocType(&msl_core_source_match_state()->player.alloc_data,
                             MSL_RELOC_POINTER_ARRAY, 8 / sizeof(void*),
                             sizeof(void*));
}
#endif

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

static void input_main_stick(const MslCoreInputPlayer* input, int8_t* x,
                             int8_t* y)
{
    if (input->nml_valid & MSL_CORE_INPUT_NML_MAIN_VALID) {
        *x = input->nml_main_x;
        *y = input->nml_main_y;
    } else {
        source_clamp_stick(input->main_x, input->main_y, x, y);
    }
}

static void input_c_stick(const MslCoreInputPlayer* input, int8_t* x,
                          int8_t* y)
{
    if (input->nml_valid & MSL_CORE_INPUT_NML_C_VALID) {
        *x = input->nml_c_x;
        *y = input->nml_c_y;
    } else {
        source_clamp_stick(input->c_x, input->c_y, x, y);
    }
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

    input_main_stick(input, &main_x, &main_y);
    input_c_stick(input, &c_x, &c_y);
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

    input_main_stick(input, &main_x, &main_y);
    input_c_stick(input, &c_x, &c_y);
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
    if (config->num_players != 0 &&
        (config->num_players < 2 || config->num_players > 4)) {
        fprintf(stderr,
                "current core requires num_players=2, 3, or 4 (or zero default)\n");
        return -1;
    }
    if (config->num_players == 0) {
        config->num_players = 2;
    }
    for (i = 0; i < config->num_players; ++i) {
        if (config->players[i].char_id != MSL_CORE_CHAR_MARIO &&
            config->players[i].char_id != MSL_CORE_CHAR_DRMARIO &&
            config->players[i].char_id != MSL_CORE_CHAR_FOX &&
            config->players[i].char_id != MSL_CORE_CHAR_CAPTAIN_FALCON &&
            config->players[i].char_id != MSL_CORE_CHAR_SHEIK &&
            config->players[i].char_id != MSL_CORE_CHAR_PEACH &&
            config->players[i].char_id != MSL_CORE_CHAR_SAMUS &&
            config->players[i].char_id != MSL_CHAR_MEWTWO &&
            config->players[i].char_id != MSL_CHAR_GAMEWATCH &&
            config->players[i].char_id != MSL_CORE_CHAR_NESS &&
            config->players[i].char_id != MSL_CORE_CHAR_LINK &&
            config->players[i].char_id != MSL_CORE_CHAR_YOUNG_LINK &&
            config->players[i].char_id != MSL_CORE_CHAR_POPO &&
            config->players[i].char_id != MSL_CORE_CHAR_DONKEY &&
            config->players[i].char_id != MSL_CORE_CHAR_GANONDORF &&
            config->players[i].char_id != MSL_CHAR_YOSHI &&
            config->players[i].char_id != MSL_CHAR_BOWSER &&
            config->players[i].char_id != MSL_CORE_CHAR_PIKACHU &&
            config->players[i].char_id != MSL_CHARACTER_PICHU &&
            config->players[i].char_id != MSL_CORE_CHAR_KIRBY &&
            config->players[i].char_id != MSL_CORE_CHAR_JIGGLYPUFF &&
            config->players[i].char_id != MSL_CORE_CHAR_LUIGI &&
            config->players[i].char_id != MSL_CORE_CHAR_MARTH &&
            config->players[i].char_id != MSL_CHARACTER_ROY &&
            config->players[i].char_id != MSL_CORE_CHAR_ZELDA &&
            config->players[i].char_id != MSL_CORE_CHAR_FALCO) {
            fprintf(stderr,
                    "current core supports external char_id=0 Mario, char_id=1 Fox, "
                    "char_id=2 Captain Falcon, char_id=3 Donkey Kong, "
                    "char_id=7 Sheik, char_id=9 Peach, char_id=10 Ice Climbers, "
                    "char_id=12 Pikachu, char_id=23 Pichu, char_id=4 Kirby, char_id=13 Samus, "
                    "char_id=6 Link, char_id=8 Ness, char_id=14 Yoshi, "
                    "char_id=16 Mewtwo, "
                    "char_id=20 Young Link, char_id=5 Bowser, "
                    "char_id=15 Jigglypuff, char_id=17 Luigi, "
                    "char_id=18 Marth, char_id=26 Roy, "
                    "char_id=19 Zelda, char_id=21 Dr. Mario, char_id=22 Falco, "
                    "char_id=24 Game & Watch, "
                    "and char_id=25 Ganondorf only\n");
            return -1;
        }
        if (config->players[i].cpu_level > 9) {
            fprintf(stderr, "CPU level must be 1..9\n");
            return -1;
        }
        if (config->players[i].handicap == 0) {
            config->players[i].handicap = 9;
        } else if (config->players[i].handicap > 9) {
            fprintf(stderr, "player handicap must be in the source range 1..9\n");
            return -1;
        }
        if (config->is_teams && config->players[i].team_id > 2) {
            fprintf(stderr, "player team id must be in the source range 0..2\n");
            return -1;
        }
        if (config->players[i].start_percent > 100) {
            fprintf(stderr, "starting percent must be in 0..100\n");
            return -1;
        }
    }
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
    game_data->source.fighter.costume_lists[FTKIND_FOX] =
        (struct UnkCostumeList){ game_data->source.fighter.fox_costumes, 4 };
    game_data->source.fighter.costume_lists[FTKIND_CAPTAIN] =
        (struct UnkCostumeList){ game_data->source.fighter.falcon_costumes,
                                 6 };
    game_data->source.fighter.costume_lists[FTKIND_SEAK] =
        (struct UnkCostumeList){ game_data->source.fighter.sheik_costumes, 5 };
    game_data->source.fighter.costume_lists[FTKIND_PEACH] =
        (struct UnkCostumeList){ game_data->source.fighter.peach_costumes, 5 };
    game_data->source.fighter.costume_lists[FTKIND_SAMUS] =
        (struct UnkCostumeList){ game_data->source.fighter.samus_costumes,
                                 5 };
    game_data->source.fighter.costume_lists[FTKIND_YOSHI] =
        (struct UnkCostumeList){ game_data->source.fighter.yoshi_costumes,
                                 6 };
    game_data->source.fighter.costume_lists[FTKIND_MEWTWO] =
        (struct UnkCostumeList){ game_data->source.fighter.mewtwo_costumes, 4 };
    game_data->source.fighter.costume_lists[FTKIND_GAMEWATCH] =
        (struct UnkCostumeList){ game_data->source.fighter.gamewatch_costumes, 4 };
    game_data->source.fighter.costume_lists[FTKIND_NESS] =
        (struct UnkCostumeList){ game_data->source.fighter.ness_costumes, 4 };
    game_data->source.fighter.costume_lists[FTKIND_LINK] =
        (struct UnkCostumeList){ game_data->source.fighter.link_costumes, 5 };
    game_data->source.fighter.costume_lists[FTKIND_CLINK] =
        (struct UnkCostumeList){ game_data->source.fighter.clink_costumes,
                                 5 };
    game_data->source.fighter.costume_lists[FTKIND_POPO] =
        (struct UnkCostumeList){ game_data->source.fighter.popo_costumes, 4 };
    game_data->source.fighter.costume_lists[FTKIND_NANA] =
        (struct UnkCostumeList){ game_data->source.fighter.nana_costumes, 4 };
    game_data->source.fighter.costume_lists[FTKIND_DONKEY] =
        (struct UnkCostumeList){ game_data->source.fighter.donkey_costumes,
                                 5 };
    game_data->source.fighter.costume_lists[FTKIND_KOOPA] =
        (struct UnkCostumeList){ game_data->source.fighter.koopa_costumes, 4 };
    game_data->source.fighter.costume_lists[FTKIND_YOSHI] =
        (struct UnkCostumeList){ game_data->source.fighter.yoshi_costumes, 6 };
    game_data->source.fighter.costume_lists[FTKIND_GANON] =
        (struct UnkCostumeList){ game_data->source.fighter.ganon_costumes,
                                 5 };
    game_data->source.fighter.costume_lists[FTKIND_PIKACHU] =
        (struct UnkCostumeList){ game_data->source.fighter.pikachu_costumes,
                                 4 };
    game_data->source.fighter.costume_lists[FTKIND_PICHU] =
        (struct UnkCostumeList){ game_data->source.fighter.pichu_costumes, 4 };
    game_data->source.fighter.costume_lists[FTKIND_KIRBY] =
        (struct UnkCostumeList){ game_data->source.fighter.kirby_costumes, 6 };
    game_data->source.fighter.costume_lists[FTKIND_PURIN] =
        (struct UnkCostumeList){ game_data->source.fighter.puff_costumes, 5 };
    game_data->source.fighter.costume_lists[FTKIND_LUIGI] =
        (struct UnkCostumeList){ game_data->source.fighter.luigi_costumes,
                                 4 };
    game_data->source.fighter.costume_lists[FTKIND_MARIO] =
        (struct UnkCostumeList){ game_data->source.fighter.mario_costumes,
                                 5 };
    game_data->source.fighter.costume_lists[FTKIND_DRMARIO] =
        (struct UnkCostumeList){ game_data->source.fighter.drmario_costumes,
                                 5 };
    game_data->source.fighter.costume_lists[FTKIND_MARS] =
        (struct UnkCostumeList){ game_data->source.fighter.marth_costumes, 5 };
    game_data->source.fighter.costume_lists[FTKIND_EMBLEM] =
        (struct UnkCostumeList){ game_data->source.fighter.roy_costumes, 5 };
    game_data->source.fighter.costume_lists[FTKIND_ZELDA] =
        (struct UnkCostumeList){ game_data->source.fighter.zelda_costumes, 5 };
    game_data->source.fighter.costume_lists[FTKIND_FALCO] =
        (struct UnkCostumeList){ game_data->source.fighter.falco_costumes, 4 };
    game_data->source.item.x38 = game_data->source.item.character_articles;
    // Kirby's per-costume hat records, one per copy with costume-specific
    // models (ftkirby.c::ftKb_Init_803C9FC8 maps kinds 3, 15, 16, 22, 24).
    game_data->source.fighter.kirby_costume_hat_table[FTKIND_DONKEY] =
        game_data->source.fighter.kirby_costume_hats[0];
    game_data->source.fighter.kirby_costume_hat_table[FTKIND_PURIN] =
        game_data->source.fighter.kirby_costume_hats[1];
    game_data->source.fighter.kirby_costume_hat_table[FTKIND_MEWTWO] =
        game_data->source.fighter.kirby_costume_hats[2];
    game_data->source.fighter.kirby_costume_hat_table[FTKIND_FALCO] =
        game_data->source.fighter.kirby_costume_hats[3];
    game_data->source.fighter.kirby_costume_hat_table[FTKIND_GAMEWATCH] =
        game_data->source.fighter.kirby_costume_hats[4];
    if (msl_memory_context_init(&game_data->memory, MSL_MEMORY_GAME_DATA) != 0)
    {
        return -1;
    }
    memcpy(game_data->root, data_root, length + 1);
    game_data->bootstrap_random.value = 1;
    game_data->bootstrap_random.active = &game_data->bootstrap_random.value;
    msl_core_bind_game_data(game_data);
    msl_host_set_data_root(game_data->root);
    msl_core_fighter_animation_data_init();
    init_hsd();
    msl_effect_game_data_init(&game_data->effects);
#ifdef MSL_CORE_NATIVE
    if (preload_supported_game_data(game_data) != 0) {
        return -1;
    }
    if (msl_fighter_pose_programs_init(&game_data->fighter_pose_programs) !=
        0)
    {
        return -1;
    }
    msl_core_bind_game_data(game_data);
    msl_ft_data_bind_motion_programs();
    game_data->fingerprint = fingerprint_game_data(game_data);
    msl_core_bind_game_data(game_data);
    msl_host_finish_initialization();
    msl_native_dat_finish_initialization();
    msl_memory_finish_initialization();
#endif
    return 0;
}

void msl_core_game_data_deinit(MslCoreGameData* game_data)
{
    if (game_data == NULL) {
        return;
    }
#ifdef MSL_CORE_NATIVE
    msl_fighter_pose_programs_deinit(&game_data->fighter_pose_programs);
    msl_native_dat_context_destroy(&game_data->native_dat);
#endif
    msl_memory_context_destroy(&game_data->memory);
    memset(game_data, 0, sizeof(*game_data));
}

static int match_construct(MslCoreMatch* match,
                           const MslCoreGameData* game_data,
                           const MslCoreMatchConfig* config,
                           const MslCoreInput* previous_input);
static void write_compare(const MslCoreMatch* match, uint32_t frame_seed,
                          MslCoreCompare* compare);

// Slippi's post-frame recorder snapshots fighter positions after gameplay
// processes but before the render pass. Capture the leader and any Nana
// follower at that same boundary.
// refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
static void capture_output_positions(MslCoreMatch* match)
{
    int i;
    for (i = 0; i < match->config.num_players; ++i) {
        Fighter* fp = GET_FIGHTER(match->fighters[i]);
        match->output_pos_x[i] = fp->cur_pos.x;
        match->output_pos_y[i] = fp->cur_pos.y;
        match->output_render_visibility[i] = fp->x221F_b0;
        match->follower_output_pos_x[i] = 0.0F;
        match->follower_output_pos_y[i] = 0.0F;
        match->follower_output_render_visibility[i] = 0;
        if (match->follower_fighters[i] != NULL) {
            Fighter* follower_fp = GET_FIGHTER(match->follower_fighters[i]);
            match->follower_output_pos_x[i] = follower_fp->cur_pos.x;
            match->follower_output_pos_y[i] = follower_fp->cur_pos.y;
            match->follower_output_render_visibility[i] =
                follower_fp->x221F_b0;
        }
    }
}

int msl_core_match_init(MslCoreMatch* match, const MslCoreGameData* game_data,
                        const MslCoreMatchConfig* config,
                        const MslCoreInput* previous_input)
{
    int result;

    if (match == NULL || game_data == NULL || config == NULL ||
        previous_input == NULL)
    {
        return -1;
    }
    if (msl_core_match_storage_init(match) != 0) {
        return -1;
    }
    result = match_construct(match, game_data, config, previous_input);
    if (result != 0) {
        msl_memory_context_destroy(&match->memory);
    }
    return result;
}

int msl_core_match_storage_init(MslCoreMatch* match)
{
    if (match == NULL) {
        return -1;
    }
    memset(match, 0, sizeof(*match));
    return msl_memory_context_init(&match->memory, MSL_MEMORY_MATCH);
}

int msl_core_match_storage_bind(MslCoreMatch* match, uint8_t* arena,
                                size_t capacity)
{
    if (match == NULL || arena == NULL ||
        capacity < msl_memory_match_capacity() || match->memory.arena != NULL)
    {
        return -1;
    }
    // Batch storage is zero-filled by its enclosing allocation. Bind only the
    // compact memory context here so creating a large uninitialized batch does
    // not first-touch every cold byte in every Match value.
    msl_memory_context_bind_match(&match->memory, arena, capacity);
    return 0;
}

int msl_core_match_reset(MslCoreMatch* match, const MslCoreGameData* game_data,
                         const MslCoreMatchConfig* config,
                         const MslCoreInput* previous_input)
{
    uint8_t* arena;
    size_t capacity;
    uint8_t arena_owned;

    if (match == NULL || game_data == NULL || config == NULL ||
        previous_input == NULL || match->memory.arena == NULL)
    {
        return -1;
    }
    arena = match->memory.arena;
    capacity = match->memory.capacity;
    arena_owned = match->memory.arena_owned;
    memset(match, 0, sizeof(*match));
    msl_memory_context_reuse_match(&match->memory, arena, capacity,
                                   arena_owned);
    return match_construct(match, game_data, config, previous_input);
}

void msl_core_match_destroy(MslCoreMatch* match)
{
    if (match == NULL) {
        return;
    }
    msl_memory_context_destroy(&match->memory);
    memset(match, 0, sizeof(*match));
}

static int match_construct(MslCoreMatch* match,
                           const MslCoreGameData* game_data,
                           const MslCoreMatchConfig* config,
                           const MslCoreInput* previous_input)
{
    const MslCoreStageSpec* spec;
    int i;
#ifdef MSL_CORE_NATIVE
    uint32_t samus_count = 0;
    uint32_t ness_count = 0;
    uint32_t mewtwo_count = 0;
    uint32_t kirby_count = 0;
    uint32_t link_count = 0;
    uint32_t sheik_count = 0;
    uint32_t ics_count = 0;
    uint32_t peach_count = 0;
    u32 runtime_item_count;
#endif

    match->random.value = 1;
    match->random.active = &match->random.value;
    match->game_data = game_data;
    msl_core_bind_match(match);
    match->config = *config;
    if (validate_config(&match->config) != 0) {
        return -1;
    }
#ifdef MSL_CORE_NATIVE
    msl_reloc_begin_match(match);
#endif
    init_hsd();
#ifdef MSL_CORE_NATIVE
    init_relocation_types();
    if (msl_fighter_pose_init(&match->fighter_pose,
                              match->config.num_players) != 0) {
        return -1;
    }
#endif
    spec = stage_spec(match->config.stage_id);
    match->stage_data = spec->source;
    bind_stage_match(match);
#ifndef MSL_CORE_NATIVE
    // Costume models must outlive Match resets: the source loader caches
    // them in GameData's costume table. Preload under its memory binding
    // before Fighter_Create can allocate them from the Match arena. The PPC
    // oracle leaves GameData unsealed; native preloads at GameData init.
    // Fighter archives and animation banks retain their per-Match owners.
    // refs/melee/src/melee/ft/ftlib.c::ftLib_80087508
    // refs/melee/src/melee/ft/ftdata.c::ftData_80085820
    msl_core_bind_game_data((MslCoreGameData*) game_data);
    for (i = 0; i < match->config.num_players; ++i) {
        FighterKind kind = (FighterKind) match->config.players[i].char_id;
        int costume_id = match->config.players[i].costume_id;
        ftData_80085820(kind, costume_id);
        if (kind == FTKIND_POPO) {
            ftData_80085820(FTKIND_NANA, costume_id);
        } else if (kind == FTKIND_ZELDA) {
            ftData_80085820(FTKIND_SEAK, costume_id);
        } else if (kind == FTKIND_SEAK) {
            ftData_80085820(FTKIND_ZELDA, costume_id);
        }
    }
    msl_core_bind_match(match);
    bind_stage_match(match);
#endif
    match->frame_id = match->config.frame_id;
    for (i = 0; i < match->config.num_players; ++i) {
        int j;
        uint8_t encoded = match->config.players[i].facing_and_port;
        uint8_t port = encoded >> 1;
        match->source_slots[i] = port == 0 ? (uint8_t) i : (uint8_t) (port - 1);
        if (match->source_slots[i] >= MSL_CORE_MAX_PLAYERS) {
            fprintf(stderr, "invalid physical port mapping\n");
            return -1;
        }
        for (j = 0; j < i; ++j) {
            if (match->source_slots[i] == match->source_slots[j]) {
                fprintf(stderr, "duplicate physical port mapping\n");
                return -1;
            }
        }
    }
    msl_core_match_rules_init(&match->rules, match->config.is_teams,
                              match->config.friendly_fire,
                              match->config.match_damage_ratio,
                              match->config.online_fnmsubs_zero,
                              match->config.brawl_offscreen_damage,
                              match->config.freeze_dead_up_fall_physics,
                              match->config.ucf_cardinals_1_0_enabled,
                              match->config.ucf_shield_sdi_enabled,
                              match->config.ucf_sdi_enabled,
                              match->config.ucf_shield_drop_extended_enabled,
                              match->config.ucf_shield_drop_084_enabled);
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
    // All supported stages enter through their source StageData owners,
    // with Ground storage redirected into this Match.
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
    stage_info.x178 = spec->source->callback5;
    stage_info.x17C = spec->source->callback6;

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
    // Source callbacks retain stage actor, animation and RNG lifetimes.
    // Geometry-only maps use the audited collision epoch publication.
    Ground_801C1E94();
    spec->source->OnInit();
    spec->source->OnLoad();
    configure_headless_ground_schedule(match, spec);
    // refs/melee/src/melee/gm/gm_16AE.c::fn_8016DCC0 and fn_8016E730.
    // Keep the source owners intact: Player_InitAllPlayers also initializes
    // each slot's statistics state, and Player_80036DD8 loads the common
    // player table consumed by the scheduled statistics pass.
    Player_InitAllPlayers();
    Player_80036DD8();
    for (i = 0; i < match->config.num_players; ++i) {
        int slot = match->source_slots[i];
        int spawn_order = neutral_spawn_order(match, i);
        const Vec3* spawn = match->config.is_teams
                                ? &spec->teams_spawn[spawn_order]
                                : &spec->singles_spawn[spawn_order];
        uint8_t encoded = match->config.players[i].facing_and_port;
#ifdef MSL_CORE_NATIVE
        if (match->config.players[i].char_id == MSL_CORE_CHAR_SAMUS) {
            samus_count += 1;
        }
        if (match->config.players[i].char_id == MSL_CHAR_MEWTWO) {
            mewtwo_count += 1;
        }
        if (match->config.players[i].char_id == MSL_CORE_CHAR_KIRBY) {
            kirby_count += 1;
        }
        if (match->config.players[i].char_id == MSL_CORE_CHAR_NESS) {
            ness_count += 1;
        }
        if (match->config.players[i].char_id == MSL_CORE_CHAR_LINK ||
            match->config.players[i].char_id == MSL_CORE_CHAR_YOUNG_LINK)
        {
            link_count += 1;
        }
        // A Zelda port can transform mid-match, so it earns Sheik's chain
        // reserve as well as Sheik's item rate below.
        if (match->config.players[i].char_id == MSL_CORE_CHAR_SHEIK ||
            match->config.players[i].char_id == MSL_CORE_CHAR_ZELDA)
        {
            sheik_count += 1;
        }
        if (match->config.players[i].char_id == MSL_CORE_CHAR_POPO) {
            ics_count += 1;
        }
        if (match->config.players[i].char_id == MSL_CORE_CHAR_PEACH) {
            peach_count += 1;
        }

#endif
        float facing = (encoded >> 1) == 0 ? (i == 0 ? 1.0F : -1.0F)
                                           : ((encoded & 1) ? 1.0F : -1.0F);
        Player_SetPlayerCharacter(
            slot, source_character_kind(match->config.players[i].char_id));
        Player_SetSlottype(slot, match->config.players[i].cpu_level != 0
                                    ? Gm_PKind_Cpu : Gm_PKind_Human);
        Player_SetPlayerAndEntityCpuLevel(slot,
                                         match->config.players[i].cpu_level);
        Player_SetTeam(slot, match->config.players[i].team_id);
        Player_SetStocks(slot, match->config.stock_count);
        // Player_SetHUDDamage initializes both player entities; Fighter_Create
        // reads Player_GetDamage before entry, without damage/hitstun callbacks.
        Player_SetHUDDamage(slot, match->config.players[i].start_percent);
        Player_SetCostumeId(slot, match->config.players[i].costume_id);
        // Standard VS forwards PlayerInitData.handicap into the player owner;
        // grab duration and several damage formulas read it at runtime.
        // refs/melee/src/melee/gm/gm_16AE.c::fn_8016D8AC
        Player_SetHandicap(slot, match->config.players[i].handicap);
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
        Player_80032768(slot, (Vec3*) spawn);
    }
    // The versus bootstrap initializes fighter/device/item allocation before
    // Fighter_Create. Items are disabled in the current Fox/FD domain, so the
    // exact Item_80266FA8(false) entry is equivalent to Item_80266F70's source
    // gate without retaining the scene-owned item-switch aggregate.
    Item_80266FA8();
    Item_80266FCC();
    Player_80036DA4();
#ifndef MSL_CORE_NATIVE
    // Retail's match load runs ftLib_80087508 -> ftData_800857E0 ->
    // ftKb_Init_UnkMotionStates5 -> Player_80031DC8(ftKb_SpecialN_800EED50)
    // for every present kind, so each Kirby's copy archive and per-costume
    // hat records exist before the first swallow. The hosted construction
    // never reaches those retail loaders: the native build preloads every
    // copy into GameData (preload_match_configuration), and the PPC oracle
    // loads them here into the match arena for the kinds actually present.
    // refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::ftKb_SpecialN_800EED50
    for (i = 0; i < match->config.num_players; ++i) {
        int j;
        if (match->config.players[i].char_id != MSL_CORE_CHAR_KIRBY) {
            continue;
        }
        for (j = 0; j < match->config.num_players; ++j) {
            ftKb_SpecialN_800EED50((s32) match->config.players[j].char_id,
                                   match->config.players[i].costume_id);
            if (match->config.players[j].char_id == MSL_CORE_CHAR_POPO) {
                ftKb_SpecialN_800EED50(FTKIND_NANA,
                                       match->config.players[i].costume_id);
            }
        }
    }
#endif
    for (i = 0; i < match->config.num_players; ++i) {
        int slot = match->source_slots[i];
        // gm_16AE.c::fn_8016E2BC creates match fighters through the player
        // owner so player_entity/transformation state and scheduled player
        // bookkeeping refer to the same GObj.
        Player_80031AD0(slot);
        match->fighters[i] = Player_GetEntityAtIndex(slot, 0);
        // Only the Ice Climbers follower branch spawns an independently
        // simulated entity 1; Sheik/Zelda's entity 1 is the dormant
        // transformation half and stays out of the follower axis.
        // refs/melee/src/melee/pl/player.c::Player_80031AD0
        match->follower_fighters[i] =
            match->config.players[i].char_id == MSL_CORE_CHAR_POPO
                ? Player_GetEntityAtIndex(slot, 1)
                : NULL;
        if (match->fighters[i] == NULL) {
            fprintf(stderr, "source Fighter_Create returned NULL for slot %d\n",
                    i);
            return -1;
        }
        if (match->config.players[i].char_id == MSL_CORE_CHAR_POPO &&
            match->follower_fighters[i] == NULL)
        {
            fprintf(stderr,
                    "source follower Fighter_Create returned NULL for slot "
                    "%d\n",
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
    for (i = 0; i < match->config.num_players; ++i) {
        int entity_index;
        for (entity_index = 0; entity_index < 2; ++entity_index) {
            Fighter_GObj* fighter_gobj = Player_GetEntityAtIndex(
                match->source_slots[i], entity_index);
            Fighter* fp;
            FigaTree* active_tree;
            void* active_archive;
            int msid;

            if (fighter_gobj == NULL) {
                continue;
            }
            fp = GET_FIGHTER(fighter_gobj);
            active_tree = fp->x590;
            active_archive = fp->x5A4;
            // Native DAT graphs widen pointers once during match
            // initialization. Preload both halves of the Sheik/Zelda source
            // transformation pair as well as every ordinary fighter so a
            // runtime swap cannot trigger lazy archive translation.
            // refs/melee/src/melee/ft/ftdata.c::{ftData_80085A14,
            //   ftData_80085CD8}
            // refs/melee/src/melee/pl/player.c::Player_80031AD0
            for (msid = 0; msid < fp->x58C; ++msid) {
                if (ftData_80085FD4(fp, msid)->x14 != 0) {
                    ftData_80085CD8(fp, fp, msid);
                }
            }
            fp->x590 = active_tree;
            fp->x5A4 = active_archive;
        }
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
    // Baselib's source allocators grow one object at a time on demand. Seal
    // only explicit runtime-reachable pools; fighter construction pools and
    // unused renderer lists keep their construction high-water without a
    // replicated speculative free slab.
    // refs/melee/src/sysdolphin/baselib/objalloc.c::{HSD_ObjAlloc,
    //   HSD_ObjAllocAddFree}
    // refs/melee/src/melee/it/item.c::{Item_80266FA8,it_8026B3A8}
    // refs/melee/src/sysdolphin/baselib/{aobj.c,fobj.c,id.c,mtx.c,robj.c}
    // tests/melee_core/runtime_census.c
    // refs/melee/src/melee/gr/grstory.c
    // Samus's grapple-catch loads one AObj per animated joint of the beam
    // link chain alongside its FObj tracks, so this reserve is per-fighter
    // too: four-Samus air grapple-catches measured 151 live AObjs (5 / 46 /
    // 91 / 151 for zero, one, two, and four Samus) against the former
    // 136-slot capacity.
    // A Kirby swallow builds the copied hat during play: the copy loader
    // (ftKb_SpecialN_800EFB4C and its siblings) takes an x2040 parts record,
    // loads the hat joint tree with its DObj/MObj/PObj pieces and matrices,
    // and the copied special's own animation and article objects follow.
    // Retail allocates all of that lazily; the sealed arena reserves it here
    // for every Kirby in the match.
    // refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::ftKb_SpecialN_800EFB4C
    HSD_ObjAllocEnsureFree(&fighter_x2040_alloc_data, 4 * kirby_count);
    {
        MslSourceMatchState* state = msl_core_source_match_state();
        uint32_t n;
        state->kirby_iso_count = 0;
        state->kirby_iso_next = 0;
        for (n = 0; n < 8 * kirby_count && n < 8 * MSL_CORE_MAX_PLAYERS; ++n) {
            state->kirby_iso_joints[n] = HSD_MemAlloc(sizeof(HSD_Joint));
            state->kirby_iso_count += 1;
        }
    }
    HSD_ObjAllocEnsureFree(HSD_AObjGetAllocData(),
                           128 + 64 * samus_count + 64 * mewtwo_count +
                               64 * kirby_count);
    // Article animation loads a whole joint graph in a single frame, so the
    // FObj bound is the sum of the concurrent per-fighter bursts. Every port
    // can contribute its own, and bursts from different fighters overlap, so
    // neither a match-wide constant nor a per-fighter maximum bounds a mixed
    // lineup. Ness x2 + Samus x2 measures 356 live FObjs from the in-tree
    // scenario and 405 from a harder driver, against 359 for four-Ness and
    // 362 for four-Samus; a maximum rule covered that case with only about a
    // quarter of the pool spare, the thinnest margin in the tree.
    // Ness's PK Flash detonation walks the explosion joint tree through
    // itNessPKFlashExplode_UnkMotion0_Anim / Item_80268BE0 / HSD_JObjAddAnim
    // and takes about 80 FObjs per simultaneous detonation.
    // Samus's grapple deploy animation runs across the beam link chain
    // (itsamusgrapple.c::it_802B743C via ftCo_AirCatch_Anim, doubled on the
    // catch), and every link jobj takes one FObj per track: about 90 per
    // simultaneous ground grapple, measured 92 / 182 / 362 live for one, two,
    // and four Samus. The air grapple-catch that motivated the original flat
    // 512 could not be measured -- four-Samus air exhausts the GObj and class
    // mem-piece pools first -- so Samus keeps its established 256 per fighter
    // rather than being reduced to Ness's measured rate.
    // refs/melee/src/melee/it/items/{itnesspkflashexplode.c,itsamusgrapple.c}
    // tests/melee_core/article_pool_smoke.c
    // Chaos soaks measured two more lineups sitting close to the flat base:
    // four Link peaked at 258 live FObjs and four Peach at 240 against the
    // 283-slot capacity it bought them. Link and Young Link overlap bow,
    // boomerang, bomb, and hookshot track loads; Peach overlaps turnip and
    // parasol graphs; both earn explicit terms.
    // tests/melee_core/pool_chaos_soak.c
    // Five forward-throw Shadow Ball graphs use 185 tracks per Mewtwo.
    // Reserve that burst per fighter; the 20-shot stress leaves at least 20%
    // headroom. it_802C519C -> Item_80268E5C; mewtwo_smoke.c.
    HSD_ObjAllocEnsureFree(HSD_FObjGetAllocData(),
                           256 + 128 * ness_count + 256 * samus_count +
                               32 * link_count + 32 * peach_count +
                               192 * mewtwo_count + 256 * kirby_count);
    HSD_ObjAllocEnsureFree(HSD_IDGetAllocData(), 128 + 32 * kirby_count);
    // Link and Young Link carry four RObjs each against two for every other
    // supported fighter, so four Link ports land on exactly 16 live RObjs and
    // filled the former flat 16-slot reserve to the last slot. Nothing is live
    // at seal, so that reserve was the whole capacity and any additional
    // consumer in the same match would have aborted.
    // refs/melee/src/sysdolphin/baselib/robj.c
    HSD_ObjAllocEnsureFree(HSD_RObjGetAllocData(), 16 + 8 * link_count);
    // Each grapple beam link is its own GObj, so the same per-fighter bound
    // applies: four-Samus air grapple-catches measured 170 live GObjs
    // (21 / 60 / 106 / 170 for zero, one, two, and four Samus) against the
    // former 142-slot capacity.
    HSD_ObjAllocEnsureFree(&gobj_alloc_data,
                           128 + 64 * samus_count + 32 * link_count +
                               32 * sheik_count + 64 * ics_count +
                               32 * kirby_count);
    // Every live item GObj schedules a proc, and Ness's steered PK Thunder
    // plus PK Fire pillars keep the most item GObjs alive at once: an
    // hour-long four-Ness chaos soak peaked at 365 live procs against the
    // former flat 256, aborting in HSD_GObj_SetupProc. Two Ness beside two
    // Pikachu measured 267, so one Ness already leaves the flat figure thin.
    // tests/melee_core/pool_chaos_soak.c
    HSD_ObjAllocEnsureFree(&gobjproc_alloc_data, 256 + 64 * ness_count);
    // The reached Peach article graph consumes twelve temporary matrix-pool
    // slots. The resulting fifteen-item bound rounds to the established
    // 256-slot small-object reserve.
    // refs/melee/src/sysdolphin/baselib/mtx.c::{HSD_MtxAlloc,HSD_MtxFree}
    // Four-Samus chaos peaked at 240 live matrix-pool slots of the flat 256,
    // six percent spare; every other lineup stays under 224. Give Samus the
    // margin explicitly rather than leaving it to luck.
    // tests/melee_core/pool_chaos_soak.c
    HSD_ObjAllocEnsureFree(HSD_MtxGetAllocData(),
                           256 + 32 * samus_count + 96 * kirby_count);
    // Matrix setup lazily allocates at most one scale Vec per fighter joint.
    // Reserve the complete constructed pose, including followers and dormant
    // transformation halves, plus the existing item/effect headroom.
    // refs/melee/src/sysdolphin/baselib/jobj.c::HSD_JObjMakeMatrix
    // refs/melee/src/sysdolphin/baselib/mtx.c::{HSD_VecAlloc,HSD_VecFree}
    HSD_ObjAllocEnsureFree(HSD_VecGetAllocData(),
                           match->fighter_pose.joint_count + 128 +
                               96 * kirby_count);
    // Retain dev's full source item reserve and the incoming per-tether link
    // reserves. Shy Guys are a separate stage-owned producer; grStory_801E3418
    // admits one wave of at most five while no prior wave remains alive.
    runtime_item_count = msl_item_reserve_runtime_pools(
        peach_count != 0,
        match->config.stage_id == MSL_CORE_STAGE_YOSHIS_STORY ? 5u : 0u,
        151 + 64 * samus_count + 24 * link_count + 32 * sheik_count +
            48 * ics_count);
    // The compact construction graph no longer leaves renderer JObjs on the
    // class free list. Preserve runtime headroom for the reached source class
    // sizes: supported item/effect graphs can cross the former 64-piece JObj
    // reserve while the Match arena is sealed.
    // hsdPreallocateMemPieces skips every size class not reached by this
    // Match, so this does not restore the former all-class slab reserve.
    // Per-fighter scaling lives on the targeted JObj floor below; every
    // measured class-piece burst -- Samus grapples, tether chains, item and
    // explosion graphs -- lands in the JObj size class, so the generic
    // minimum only has to cover the small classes' modest churn.
    // refs/melee/src/sysdolphin/baselib/{class.c,jobj.c}
    hsdPreallocateMemPieces(128 + 96 * kirby_count);
    // Tethers need a per-port JObj floor even if construction did not touch
    // the class. Peach additionally covers her full item reserve: each
    // PlPe.dat turnip graph has 17 joints (it_802BD4AC -> Item_802680CC).
    msl_class_reserve_pieces(
        sizeof(HSD_JObj), 128 + 128 * match->config.num_players +
                              (peach_count != 0 ? 17 * runtime_item_count : 0));

    // This Match's source allocation pools are complete. Shared DAT graphs
    // are sealed once, after GameData has preloaded the supported domain;
    // sealing them here would make the first Match configuration determine
    // which characters and stages later Match instances may construct.
    if (msl_reloc_seal_match(match) != 0) {
        return -1;
    }
    msl_memory_finish_initialization();
#endif

    // MslCoreMatchConfig names this as the seed immediately before the first
    // simulated frame, so constructor-time random choices do not consume it.
    seed = match->config.frame_pre_random_seed;
    seed_ptr = &seed;
    match->last_frame_seed = seed;
    capture_output_positions(match);
    match->random_seed = seed;
    return 0;
}

#ifdef MSL_CORE_NATIVE
static int preload_match_configuration(MslCoreGameData* game_data,
                                       uint8_t stage_id, uint8_t char_id,
                                       uint8_t opponent_id)
{
    MslCoreMatch* match;
    MslCoreMatchConfig config;
    MslCoreInput previous_input;
    int result;

    match = calloc(1, sizeof(*match));
    if (match == NULL) {
        return -1;
    }
    memset(&config, 0, sizeof(config));
    memset(&previous_input, 0, sizeof(previous_input));
    config.stage_id = stage_id;
    config.frame_id = -123;
    config.frame_pre_random_seed = 1;
    config.initial_random_seed = 1;
    config.match_damage_ratio = 1.0F;
    config.num_players = 2;
    config.stock_count = 4;
    config.players[0].char_id = char_id;
    config.players[1].char_id = opponent_id;
    result = msl_core_match_init(match, game_data, &config, &previous_input);
    if (result == 0) {
        int costume_id;
        int kind;

        // Kirby's copy archives load lazily in retail: match start walks
        // the player slots and loads each present kind's copy for the
        // configured costume (ftKb_Init_UnkMotionStates5 -> Player_80031DC8
        // -> ftKb_SpecialN_800EED50). Sealed GameData cannot load during
        // play, so a Kirby preload against every supported kind runs the
        // same loader for all six Kirby costumes here; the hats and
        // per-costume hat models then live in GameData for every match.
        // refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::
        //   {ftKb_Init_UnkMotionStates5,ftKb_SpecialN_800EED50}
        if (char_id == MSL_CORE_CHAR_KIRBY) {
            for (costume_id = 0;
                 costume_id < CostumeListsForeachCharacter[FTKIND_KIRBY].numCostumes;
                 ++costume_id)
            {
                ftKb_SpecialN_800EED50((s32) opponent_id, costume_id);
                if (opponent_id == MSL_CORE_CHAR_POPO) {
                    ftKb_SpecialN_800EED50(FTKIND_NANA, costume_id);
                }
            }
            // The swallow also publishes the copy's article pointers into
            // the character-item catalog (ftKb_SpecialN_800F16D0). That
            // catalog is GameData and read-only once sealed, so publish
            // them here; the play-time registration then finds the same
            // pointers and skips its write.
            // refs/melee/src/melee/it/it_26B1.c::it_8026B3F8
            ftKb_SpecialN_800F16D0(NULL, (FighterKind) opponent_id);
        }

        for (kind = 0; kind < FTKIND_MAX; ++kind) {
            if (match->source.fighter.data_list[kind] != NULL) {
                game_data->source.fighter.data_list[kind] =
                    match->source.fighter.data_list[kind];
            }
        }

        // Fighter_Create reaches only the configured costume, while a public
        // Match may select any source-declared costume after immutable
        // GameData is sealed. Translate every costume archive once during
        // the owning fighter-family preload.
        // refs/melee/src/melee/ft/{fighter.c::Fighter_Create,
        //   ftdata.c::ftData_80085820}
        for (costume_id = 0;
             costume_id < CostumeListsForeachCharacter[char_id].numCostumes;
             ++costume_id)
        {
            ftData_80085820((FighterKind) char_id, costume_id);
        }
        // Ice Climbers construct two fighters per slot; Nana's costume
        // archives pair with Popo's ids and are reached by the same match.
        // refs/melee/src/melee/pl/player.c::Player_80031AD0
        if (char_id == MSL_CORE_CHAR_POPO) {
            for (costume_id = 0;
                 costume_id <
                 CostumeListsForeachCharacter[FTKIND_NANA].numCostumes;
                 ++costume_id)
            {
                ftData_80085820(FTKIND_NANA, costume_id);
            }
        }
    }
    msl_memory_context_destroy(&match->memory);
    free(match);
    return result;
}

static int preload_supported_game_data(MslCoreGameData* game_data)
{
    static const uint8_t characters[] = {
        MSL_CORE_CHAR_FOX,        MSL_CORE_CHAR_FALCO,
        MSL_CORE_CHAR_MARTH,      MSL_CORE_CHAR_CAPTAIN_FALCON,
        MSL_CHARACTER_ROY,
        MSL_CORE_CHAR_SHEIK,      MSL_CORE_CHAR_ZELDA,
        MSL_CORE_CHAR_JIGGLYPUFF, MSL_CORE_CHAR_PEACH,
        MSL_CORE_CHAR_LUIGI,
        MSL_CORE_CHAR_MARIO,
        MSL_CORE_CHAR_DRMARIO,
        MSL_CORE_CHAR_SAMUS,
        MSL_CORE_CHAR_POPO,
        MSL_CORE_CHAR_PIKACHU,
        MSL_CHARACTER_PICHU,
        MSL_CORE_CHAR_KIRBY,
        MSL_CORE_CHAR_DONKEY,
        MSL_CORE_CHAR_GANONDORF,
        MSL_CHAR_YOSHI,
        MSL_CHAR_BOWSER,
        MSL_CHAR_MEWTWO,
        MSL_CHAR_GAMEWATCH,
        MSL_CORE_CHAR_NESS,
        MSL_CORE_CHAR_LINK,
        MSL_CORE_CHAR_YOUNG_LINK,
    };
    size_t i;

    // Preload one source-shaped match per fighter family on FD, then one Fox
    // match for every remaining supported stage. Archive/raw bytes and native
    // DAT graphs are cached in GameData, while the temporary runtime arenas
    // are discarded. This keeps ordinary Match construction free of file I/O
    // and makes the immutable domain independent of the first public config.
    // refs/melee/src/melee/gm/gm_16AE.c::fn_8016E730
    // refs/melee/src/melee/lb/lbarchive.c::{lbArchive_LoadArchive,
    //   lbArchive_LoadSymbols}
    for (i = 0; i < sizeof(characters) / sizeof(characters[0]); ++i) {
        if (preload_match_configuration(game_data,
                                        MSL_CORE_STAGE_FINAL_DESTINATION,
                                        characters[i], characters[i]) != 0)
        {
            return -1;
        }
    }
    // Kirby against every kind, so each copy ability's archive and hat
    // models are resident before GameData seals (see
    // preload_match_configuration).
    for (i = 0; i < sizeof(characters) / sizeof(characters[0]); ++i) {
        if (characters[i] == MSL_CORE_CHAR_KIRBY) {
            continue;
        }
        if (preload_match_configuration(game_data,
                                        MSL_CORE_STAGE_FINAL_DESTINATION,
                                        MSL_CORE_CHAR_KIRBY,
                                        characters[i]) != 0)
        {
            return -1;
        }
    }
    for (i = 0; i < sizeof(stage_specs) / sizeof(stage_specs[0]); ++i) {
        if (stage_specs[i].external_id == MSL_CORE_STAGE_FINAL_DESTINATION) {
            continue;
        }
        if (preload_match_configuration(game_data, stage_specs[i].external_id,
                                        MSL_CORE_CHAR_FOX, MSL_CORE_CHAR_FOX) != 0)
        {
            return -1;
        }
    }
    return 0;
}
#endif

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
    // Kirby's copies share their origin item's variable layout, so the
    // pointer-widening remaps below apply to them through the origin kind
    // (a copied Thunder Jolt otherwise sampled its widened owner pointer
    // instead of the crawl position's low bytes).
    ItemKind kind = (ItemKind) msl_core_item_lane_origin_kind(item->kind);

    // Preserve source offsets after native pointer widening. These generic
    // Slippi lanes land after pointers in the Chain, Din's Fire and Chef structs,
    // so indexing the native union by the retail byte offset would sample a
    // different member. The selected source bytes are the low bytes of the
    // named 32-bit gameplay scalars.
    // refs/melee/src/melee/it/{itCommonItems.h,itCharItems.h}
    // refs/slippi-ssbm-asm/Recording/SendItemInfo.s
    if (kind == It_Kind_Yoshi_EggThrow ||
        kind == It_Kind_Yoshi_Star ||
        (kind == It_Kind_Yoshi_EggLay && source_offset != 3))
    {
        // EggThrow/Star never write item vars; EggLay owns only xDD4.
        // Reused native union storage can contain widened host pointers.
        // Keep these unowned source bytes deterministic, as for Blizzard.
        return 0;
    }
    if (kind == It_Kind_GameWatch_Chef && source_offset == 7) {
        return (uint8_t) item->xDD4_itemVar.gamewatchchef.x4;
    }
    if (kind == It_Kind_IceClimber_Ice) {
        if (source_offset == 7) {
            memcpy(&word, &item->xDD4_itemVar.climbersice.x4, sizeof(word));
            return (uint8_t) word;
        }
    } else if (kind == It_Kind_Seak_Chain) {
        if (source_offset == 0x17) {
            memcpy(&word, &item->xDD4_itemVar.seakchain.x14, sizeof(word));
            return (uint8_t) word;
        }
        if (source_offset == 0x1B) {
            memcpy(&word, &item->xDD4_itemVar.seakchain.x18, sizeof(word));
            return (uint8_t) word;
        }
    } else if (kind == It_Kind_Zelda_DinFire) {
        if (source_offset == 0x17) {
            memcpy(&word, &item->xDD4_itemVar.zeldadinfire.xDE8,
                   sizeof(word));
            return (uint8_t) word;
        }
        if (source_offset == 0x1B) {
            memcpy(&word, &item->xDD4_itemVar.zeldadinfire.xDEC,
                   sizeof(word));
            return (uint8_t) word;
        }
    } else if (kind == It_Kind_IceClimber_Ice) {
        // Retail layout: +0 Item_GObj* x0 (owner), +4 f32 x4 (scale),
        // +8 flag bits. Offset 3 samples the owner pointer's low byte and
        // offset 7 the live scale; 0x17/0x1B fall in pool residue past the
        // declared members.
        if (source_offset == 3) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.climbersice.x0;
        }
        if (source_offset == 7) {
            memcpy(&word, &item->xDD4_itemVar.climbersice.x4, sizeof(word));
            return (uint8_t) word;
        }
        if (source_offset == 0x17 || source_offset == 0x1B) {
            return 0;
        }
    } else if (kind == It_Kind_Seak_NeedleHeld) {
        // Retail layout is a single Fighter_GObj* owner; offset 3 samples its
        // low byte and 4..7 sit past the declared member. The widened native
        // pointer's upper half is host-mapping-dependent, so pin the
        // past-member byte instead of leaking it.
        if (source_offset == 3) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.seakneedleheld
                .owner;
        }
        if (source_offset == 7) {
            return 0;
        }
    } else if (kind == It_Kind_IceClimber_Blizzard) {
        // Retail layout is f32 x0 + one flag byte; offsets 7/0x17/0x1B all
        // sit past the declared members in allocator-reuse residue. Pin them
        // so the native union's previous-occupant bytes do not leak.
        if (source_offset == 7 || source_offset == 0x17 ||
            source_offset == 0x1B)
        {
            return 0;
        }
    } else if (kind == It_Kind_IceClimber_GumStrings) {
        // Retail layout: +0 f32 x0, +4/+8 ItemLink*, +C HSD_GObj*, +14
        // HSD_JObj*. Offsets 7 and 0x17 sample the x4 link and x14 joint
        // pointer low bytes; 0x1B is past the declared members.
        if (source_offset == 7) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.climbersstring.x4;
        }
        if (source_offset == 0x17) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.climbersstring.x14;
        }
        if (source_offset == 0x1B) {
            return 0;
        }
    } else if (kind == It_Kind_Pikachu_TJolt_Ground ||
               kind == It_Kind_Pichu_TJolt_Ground) {
        // Retail layout: +0 f32 xDD4 (crawl angle), +4 HSD_GObj* xDD8
        // (owner), +8 Item_GObj* xDDC, +C/+10 s32, +14 Vec3 xDE8 (spawn/
        // crawl position). Offset 3 samples the unshifted leading angle
        // through the generic path; offset 7 is the owner pointer's low
        // byte and 0x17/0x1B the position's x/y low bytes, all displaced
        // by the two widened pointers.
        if (source_offset == 7) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.pikachujoltground
                .xDD8;
        }
        if (source_offset == 0x17) {
            memcpy(&word, &item->xDD4_itemVar.pikachujoltground.xDE8.x,
                   sizeof(word));
            return (uint8_t) word;
        }
        if (source_offset == 0x1B) {
            memcpy(&word, &item->xDD4_itemVar.pikachujoltground.xDE8.y,
                   sizeof(word));
            return (uint8_t) word;
        }
    } else if (kind == It_Kind_Ness_PKThunder) {
        // Retail layout: +0 HSD_GObj* xDD4[6] (the six trail articles),
        // +0x18 Vec3 positions[16]. Offsets 3/7/0x17 sample trail pointer
        // low bytes and 0x1B the first recorded position's x, all displaced
        // by the six widened pointers.
        // refs/melee/src/melee/it/itPKThunder.h::itPKThunder_ItemVars
        if (source_offset == 3) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.pkthunder
                .xDD4[0];
        }
        if (source_offset == 7) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.pkthunder
                .xDD4[1];
        }
        if (source_offset == 0x17) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.pkthunder
                .xDD4[5];
        }
        if (source_offset == 0x1B) {
            memcpy(&word, &item->xDD4_itemVar.pkthunder.positions[0].x,
                   sizeof(word));
            return (uint8_t) word;
        }
    } else if (kind == It_Kind_Ness_PKThunder1 ||
               kind == It_Kind_Ness_PKThunder2 ||
               kind == It_Kind_Ness_PKThunder3 ||
               kind == It_Kind_Ness_PKThunder4)
    {
        // Retail layout: +0 Item_GObj* x0 (the ball), +4 s32 x4, +8 s32 x8.
        // Offset 3 samples the ball pointer's low byte and offset 7 the
        // trail index displaced by that widened pointer; 0x17/0x1B sit past
        // the declared members.
        // refs/melee/src/melee/it/itCharItems.h::itNesspkthundertrail_ItemVars
        if (source_offset == 3) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar
                .nesspkthundertrail.x0;
        }
        if (source_offset == 7) {
            memcpy(&word, &item->xDD4_itemVar.nesspkthundertrail.x4,
                   sizeof(word));
            return (uint8_t) word;
        }
    } else if (kind == It_Kind_Ness_Bat) {
        // Retail layout is a single owner HSD_GObj*; offset 3 samples its
        // low byte and 7/0x17/0x1B sit past the declared member.
        // refs/melee/src/melee/it/itCharItems.h::itNessbat_ItemVars
        if (source_offset == 3) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.nessbat.x0;
        }
    } else if (kind == It_Kind_Ness_Yoyo) {
        // Retail layout: +0 s32 x0, +4 f32 x4, +8/+C ItemLink*, +10
        // HSD_GObj*, +14 pad, +18 HSD_JObj*. Offsets 3 and 7 reach the
        // leading scalars through the generic path; 0x1B samples the string
        // joint pointer's low byte, displaced by the three widened pointers.
        // refs/melee/src/melee/it/itCharItems.h::itNessYoyo_ItemVars
        if (source_offset == 0x1B) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.nessyoyo.x18;
        }
    } else if (kind == It_Kind_Pikachu_TJolt_Air ||
               kind == It_Kind_Pichu_TJolt_Air) {
        // Retail layout: +0 HSD_GObj* xDD4 (owner), +4 Item_GObj* xDD8
        // (ground-jolt sibling), +8 pad, +14 Vec3 xDE8 (launch velocity).
        // Offsets 3/7 sample the pointer low bytes and 0x17/0x1B the
        // velocity's x/y low bytes.
        if (source_offset == 3) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.pikachujoltair
                .xDD4;
        }
        if (source_offset == 7) {
            return (uint8_t) (uintptr_t) item->xDD4_itemVar.pikachujoltair
                .xDD8;
        }
        if (source_offset == 0x17) {
            memcpy(&word, &item->xDD4_itemVar.pikachujoltair.xDE8.x,
                   sizeof(word));
            return (uint8_t) word;
        }
        if (source_offset == 0x1B) {
            memcpy(&word, &item->xDD4_itemVar.pikachujoltair.xDE8.y,
                   sizeof(word));
            return (uint8_t) word;
        }
    }

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

static void write_item_compare(uint8_t* item_out, Item_GObj* gobj)
{
    Item* item = GET_ITEM(gobj);
    int8_t owner = -1;

    // Recording/SendItemInfo.s follows the owner GObj and reads the player
    // slot from user-data byte 0xC. Fox articles retain their fighter owner
    // for their complete lifetime, so the same source layout applies here.
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

int msl_core_write_items_into_zeroed(
    const MslCoreMatch* match, MslCoreItem items[MSL_CORE_MAX_ITEMS])
{
    Item_GObj* item_gobj;
    int item_slot = 0;

    item_gobj = (Item_GObj*) match->gobj.entities->items;
    while (item_gobj != NULL && item_slot < MSL_CORE_MAX_ITEMS) {
        write_item_compare((uint8_t*) &items[item_slot++], item_gobj);
        item_gobj = (Item_GObj*) item_gobj->next;
    }
    return item_slot;
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
    out[offsetof(MslCoreCompare, num_players)] = match->config.num_players;
    out[offsetof(MslCoreCompare, is_teams)] = match->config.is_teams ? 1 : 0;
    // MslCoreCompare represents all four controller slots. Slots without a
    // source Fighter GObj use the validation contract's inactive/dead value.
    for (i = match->config.num_players; i < MSL_CORE_MAX_PLAYERS; ++i) {
        out[offsetof(MslCoreCompare, is_dead) + i] = 1;
    }

    for (i = 0; i < match->config.num_players; ++i) {
        Fighter* fp = GET_FIGHTER(match->fighters[i]);
        uint8_t state_flags[5];
        float hitstun = fp->x221C_b6 ? fp->mv.co.damage.x0 : 0.0F;
        int hurtbox = fp->x1988 != 0 ? fp->x1988 : fp->x198C;
        int jumps_left = fp->co_attrs.max_jumps - fp->x1968_jumpsUsed;

        out[offsetof(MslCoreCompare, team_id) + i] = fp->team;
        out[offsetof(MslCoreCompare, char_id) + i] = fp->kind;
        put_player_f32(out, offsetof(MslCoreCompare, pos_x), i,
                       match->output_pos_x[i]);
        put_player_f32(out, offsetof(MslCoreCompare, pos_y), i,
                       match->output_pos_y[i]);
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
        state_flags[4] =
            (state_flags[4] & 0x7FU) |
            ppc_state_bit(match->output_render_visibility[i], 0);
        memcpy(out + offsetof(MslCoreCompare, state_flags) + i * 5, state_flags,
               sizeof(state_flags));
    }

    // Follower (Nana) lanes mirror the leader extraction above. Slippi's
    // recorder emits follower rows only while Nana's fighter is awake: her
    // Dead* animation frames still record, rows stop when ftCo_800BFD04 puts
    // her into ftCo_MS_Sleep (x221F_b3), and resume with the leader Rebirth.
    // refs/melee/src/melee/ft/ftcolanim.c::ftCo_800BFD04
    for (i = 0; i < match->config.num_players; ++i) {
        Fighter* fp;
        uint8_t state_flags[5];
        float hitstun;
        int hurtbox;
        int jumps_left;

        if (match->follower_fighters[i] == NULL) {
            continue;
        }
        fp = GET_FIGHTER(match->follower_fighters[i]);
        if (fp->x221F_b3) {
            continue;
        }
        hitstun = fp->x221C_b6 ? fp->mv.co.damage.x0 : 0.0F;
        hurtbox = fp->x1988 != 0 ? fp->x1988 : fp->x198C;
        jumps_left = fp->co_attrs.max_jumps - fp->x1968_jumpsUsed;

        out[offsetof(MslCoreCompare, follower_present) + i] = 1;
        out[offsetof(MslCoreCompare, follower_char_id) + i] = fp->kind;
        put_player_f32(out, offsetof(MslCoreCompare, follower_pos_x), i,
                       match->follower_output_pos_x[i]);
        put_player_f32(out, offsetof(MslCoreCompare, follower_pos_y), i,
                       match->follower_output_pos_y[i]);
        put_player_f32(out, offsetof(MslCoreCompare, follower_speed_air_x_self),
                       i, fp->self_vel.x);
        put_player_f32(out,
                       offsetof(MslCoreCompare, follower_speed_ground_x_self),
                       i, fp->gr_vel);
        put_player_f32(out, offsetof(MslCoreCompare, follower_speed_y_self), i,
                       fp->self_vel.y);
        put_player_f32(out, offsetof(MslCoreCompare, follower_speed_x_attack),
                       i, fp->x8c_kb_vel.x);
        put_player_f32(out, offsetof(MslCoreCompare, follower_speed_y_attack),
                       i, fp->x8c_kb_vel.y);
        out[offsetof(MslCoreCompare, follower_facing) + i] =
            fp->facing_dir > 0.0F;
        out[offsetof(MslCoreCompare, follower_on_ground) + i] =
            fp->ground_or_air == GA_Ground;
        put_player_u16(out, offsetof(MslCoreCompare, follower_action_id), i,
                       (uint16_t) fp->motion_id);
        put_player_u16(out, offsetof(MslCoreCompare, follower_action_frame), i,
                       (uint16_t) state_age_i16(fp->cur_anim_frame));
        out[offsetof(MslCoreCompare, follower_jumps_left) + i] =
            jumps_left > 0 ? (uint8_t) jumps_left : 0;
        out[offsetof(MslCoreCompare, follower_stocks) + i] =
            (uint8_t) Player_GetStocks(fp->player_id);
        put_player_f32(out, offsetof(MslCoreCompare, follower_percent), i,
                       fp->dmg.x1830_percent);
        put_player_f32(out, offsetof(MslCoreCompare, follower_shield_hp), i,
                       fp->shield_health);
        put_player_u16(out, offsetof(MslCoreCompare, follower_hitlag), i,
                       float_frames_u16(fp->dmg.x195c_hitlag_frames));
        put_player_u16(out, offsetof(MslCoreCompare, follower_hitstun), i,
                       float_frames_u16(hitstun));
        out[offsetof(MslCoreCompare, follower_l_cancel) + i] =
            msl_slippi_lcancel_get(fp);
        out[offsetof(MslCoreCompare, follower_hurtbox_state) + i] =
            (uint8_t) hurtbox;
        put_player_u16(out, offsetof(MslCoreCompare, follower_ground_id), i,
                       (uint16_t) fp->coll_data.floor.index);
        put_player_u32(out, offsetof(MslCoreCompare, follower_animation_index),
                       i, (uint32_t) fp->anim_id);
        put_player_u16(out, offsetof(MslCoreCompare, follower_instance_hit_by),
                       i, fp->dmg.x18ec_instancehitby);
        put_player_u16(out, offsetof(MslCoreCompare, follower_instance_id), i,
                       fp->x2074.x2088);
        out[offsetof(MslCoreCompare, follower_last_attack_landed) + i] =
            (uint8_t) fp->x208C;
        out[offsetof(MslCoreCompare, follower_combo_count) + i] =
            (uint8_t) fp->x2090;
        out[offsetof(MslCoreCompare, follower_last_hit_by) + i] =
            (uint8_t) fp->dmg.x18c4_source_ply;
        pack_fighter_state_flags(fp, state_flags);
        state_flags[4] =
            (state_flags[4] & 0x7FU) |
            ppc_state_bit(match->follower_output_render_visibility[i], 0);
        memcpy(out + offsetof(MslCoreCompare, follower_state_flags) + i * 5,
               state_flags, sizeof(state_flags));
    }

    (void) msl_core_write_items_into_zeroed(match, compare->items);
}

static void publish_render_matrices_pass(HSD_JObj* jobj, u32 trsp_mask)
{
    HSD_JObj* child;

    if (jobj == NULL) {
        return;
    }
#ifdef MSL_CORE_HOSTED
    if (jobj->flags & JOBJ_MSL_GAMEPLAY_COLD) {
        return;
    }
#endif
    if (jobj->flags & JOBJ_INSTANCE) {
        // Visible instances publish both the instance root and referenced
        // child before traversing the shared tree.
        // refs/melee/src/sysdolphin/baselib/jobj.c::HSD_JObjDispAll
        if (!(jobj->flags & JOBJ_HIDDEN) && jobj->child != NULL) {
            HSD_JObjSetupMatrix(jobj);
            HSD_JObjSetupMatrix(jobj->child);
            publish_render_matrices_pass(jobj->child, trsp_mask);
        }
        return;
    }

    // HSD_JObjDispAll only reaches HSD_JObjDispDObj (and therefore the lazy
    // matrix setup) for a JObj participating in the current transparency
    // pass. It likewise only descends through roots carrying that pass bit.
    // Preserving those two gates matters to gameplay: collision can observe
    // matrices intentionally left at an earlier publication epoch even after
    // the corresponding local animation values have advanced.
    // refs/melee/src/sysdolphin/baselib/{jobj.c,displayfunc.c}::{
    //   HSD_JObjDispAll,HSD_JObjDisp,HSD_JObjDispDObj}
    if (!(jobj->flags & JOBJ_HIDDEN) &&
        (jobj->flags & (trsp_mask << 18)) != 0 && union_type_dobj(jobj))
    {
        HSD_JObjSetupMatrix(jobj);
    }
    if ((jobj->flags & (trsp_mask << 28)) != 0) {
        for (child = jobj->child; child != NULL; child = child->next) {
            publish_render_matrices_pass(child, trsp_mask);
        }
    }
}

static void publish_render_matrices(HSD_JObj* jobj)
{
    // HSD_GObj_80390ED0 visits the normal gameplay camera's three passes in
    // bit order; HSD_GObj_804085F0 maps them to OPA, XLU, then TEXEDGE.
    // refs/melee/src/sysdolphin/baselib/gobj.c::{
    //   HSD_GObj_804085F0,HSD_GObj_80390ED0}
    static const u32 trsp_masks[] = { 1, 4, 2 };
    int i;

    for (i = 0; i < ARRAY_SIZE(trsp_masks); ++i) {
        publish_render_matrices_pass(jobj, trsp_masks[i]);
    }
}

static void bind_step_owners(MslCoreMatch* match)
{
    msl_core_bind_match(match);
    msl_core_bind_match_rules(&match->rules);
    msl_camera_state_bind(&match->camera);
    msl_effect_projection_bind(&match->game_data->effects, &match->effects);
    msl_slippi_state_bind(&match->slippi);
    bind_stage_match(match);
}

static void bind_scheduler_owners(MslCoreMatch* match)
{
    // Scheduler interleaving changes owners often, but the next-owner and
    // invoke halves for a lane can be adjacent. The active Match is the
    // authority for the complete owner bundle bound above, so avoid writing
    // every TLS owner twice when that lane is already current.
    if (msl_core_try_active_match() != match) {
        bind_step_owners(match);
    }
}

int msl_core_match_step_prepare(MslCoreMatch* match,
                                const MslCoreInput* input,
                                uint32_t frame_seed,
                                const MslCoreStageEvents* stage_events)
{
    int i;

    if (match == NULL || input == NULL || stage_events == NULL) {
        fprintf(stderr, "Melee core scalar step received a null owner\n");
        return -1;
    }
    bind_step_owners(match);
    msl_slippi_stage_events_begin(stage_events);

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
        StructPairWithStageID stage_pair = {
            match->stage_data->internal_stage_id, match->config.stage_id
        };
        ftLib_800868A4();
        Ground_801C0FB8(&stage_pair);
    }

    for (i = 0; i < match->config.num_players; ++i) {
        int slot = match->source_slots[i];
        inject_pad_status(slot, &input->p[i]);
        msl_ucf_set_pending_pad(slot, input->p[i].main_x, input->p[i].main_y,
                                input->p[i].c_x, input->p[i].c_y);
    }
    // Versus scene OnFrame runs after pad publication and immediately before
    // the gameplay-object scheduler. Preserve the source-owned team stock
    // transfer at that same boundary.
    // refs/melee/src/melee/gm/gm_1A45.c::gm_801A4D34
    // refs/melee/src/melee/gm/gm_16AE.c::{fn_8016CFE0,fn_8016B918}
    msl_core_apply_team_stock_steal();
    return 0;
}

void msl_core_match_scheduler_begin(MslCoreMatch* match)
{
    bind_scheduler_owners(match);
    msl_hsd_gobj_run_procs_begin();
}

uint32_t msl_core_match_scheduler_priority_count(const MslCoreMatch* match)
{
    return (uint32_t) match->gobj.init_data.gproc_pri_max + 1;
}

void msl_core_match_scheduler_priority_begin(MslCoreMatch* match,
                                             uint32_t priority)
{
    bind_scheduler_owners(match);
    msl_hsd_gobj_run_procs_priority_begin((s32) priority);
}

HSD_GObjEvent msl_core_match_scheduler_next_owner(MslCoreMatch* match)
{
    bind_scheduler_owners(match);
    return msl_hsd_gobj_run_procs_next_owner();
}

void msl_core_match_scheduler_invoke(MslCoreMatch* match)
{
    bind_scheduler_owners(match);
    msl_hsd_gobj_run_procs_invoke();
}

HSD_GObjEvent msl_core_match_scheduler_invoke_owner(MslCoreMatch* match,
                                                    HSD_GObjEvent owner)
{
    HSD_GObjEvent next;
    bind_scheduler_owners(match);
    do {
        msl_hsd_gobj_run_procs_invoke();
        next = msl_hsd_gobj_run_procs_next_owner();
    } while (next == owner);
    return next;
}

int msl_core_match_step_finish(MslCoreMatch* match, uint32_t frame_seed)
{
    int i;
#ifdef MSL_SUBSYSTEM_PROFILE
    uint64_t finish_started = msl_profile_cycles();
#endif
    if (match == NULL) {
        return -1;
    }
    bind_scheduler_owners(match);
    for (i = 0; i < match->config.num_players; ++i) {
        // Player_SwapTransformedStates keeps the active Sheik/Zelda half in
        // source entity slot zero. Refresh this convenience pointer after the
        // gameplay scheduler so output and the headless render publication
        // observe the newly active fighter in the transformation frame.
        // refs/melee/src/melee/pl/player.c::Player_SwapTransformedStates
        match->fighters[i] =
            Player_GetEntityAtIndex(match->source_slots[i], 0);
        // The Nana follower entity can be re-created by respawn handling, so
        // refresh her pointer at the same boundary as the leader's.
        match->follower_fighters[i] =
            match->config.players[i].char_id == MSL_CORE_CHAR_POPO
                ? Player_GetEntityAtIndex(match->source_slots[i], 1)
                : NULL;
        if (match->fighters[i] == NULL) {
            fprintf(stderr, "source player slot %d lost its active fighter\n",
                    match->source_slots[i]);
            return -1;
        }
    }
    // Slippi's post-frame recorder snapshots fighter state after gameplay
    // processes but before the render pass. x221F_b0 therefore reflects the
    // preceding render in the exported row.
    // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    match->frame_id += 1;
    match->last_frame_seed = frame_seed;
    capture_output_positions(match);

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
    // Retail's intervening fighter draw walks the complete visible JObj tree
    // through HSD_JObjDispAll. Hosted fighter construction has no
    // DObj/MObj/PObj graph, so the only remaining Match-wide render owner is
    // camera visibility/magnifier/DeadUp publication. Items retain their
    // independent lazy-matrix publication below.
    // refs/melee/src/melee/ft/ftdrawcommon.c::{
    //   ftDrawCommon_80080E18,ftDrawCommon_800805C8}
    // refs/melee/src/sysdolphin/baselib/jobj.c::{
    //   HSD_JObjDispAll,HSD_JObjSetupMatrixSub}
    {
        // Retail's draw pass walks every fighter GObj, so a live Nana
        // follower participates in the same visibility publication as the
        // leaders.
        Fighter_GObj* visible_fighters[MSL_CORE_MAX_PLAYERS * 2];
        int visible_count = 0;
#ifdef MSL_SUBSYSTEM_PROFILE
        uint64_t started = msl_profile_cycles();
#endif
        for (i = 0; i < match->config.num_players; ++i) {
            visible_fighters[visible_count++] = match->fighters[i];
        }
        for (i = 0; i < match->config.num_players; ++i) {
            if (match->follower_fighters[i] != NULL) {
                visible_fighters[visible_count++] =
                    match->follower_fighters[i];
            }
        }
        msl_camera_publish_match_visibility(visible_fighters, visible_count);
#ifdef MSL_SUBSYSTEM_PROFILE
        msl_profile_add(MSL_PROFILE_FINISH_FIGHTER_VISIBILITY,
                             msl_profile_cycles() - started);
#endif
    }
    {
        Item_GObj* item_gobj = (Item_GObj*) HSD_GObj_Entities->items;
#ifdef MSL_SUBSYSTEM_PROFILE
        uint64_t started = msl_profile_cycles();
#endif
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
    {
        // Kirby's copy hat is a separate JObj tree that retail positions in
        // the display pass: ftKb_UnkMtxFunc0 copies the head part's world
        // matrix onto the hat root before drawing, and the copied specials
        // spawn their projectiles and place the spat fighter from hat bones
        // read on later frames. Headless has no display pass, so publish the
        // hat here with the same end-of-frame timing.
        // refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::ftKb_UnkMtxFunc0
        HSD_GObj* gobj = HSD_GObj_Entities->fighters;
        while (gobj != NULL) {
            Fighter* fp = gobj->user_data;
            if (fp != NULL && fp->kind == FTKIND_KIRBY &&
                fp->fv.kb.hat.jobj != NULL && fp->x2225_b2)
            {
                HSD_JObj* hat = fp->fv.kb.hat.jobj;
                HSD_JObjCopyMtx(hat, HSD_JObjGetMtxPtr(fp->parts[6].joint));
                hat->flags |= JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT |
                              JOBJ_MTX_INDEP_SRT;
                HSD_JObjSetMtxDirty(hat);
                publish_render_matrices(hat);
            }
            gobj = gobj->next;
        }
#ifdef MSL_SUBSYSTEM_PROFILE
        msl_profile_add(MSL_PROFILE_FINISH_ITEM_MATRICES,
                             msl_profile_cycles() - started);
#endif
    }
    // gm_8016AEDC is observed by fighter processes during this pass. The
    // source match owner advances it only after those processes have run.
    msl_core_advance_match_frame();
    match->random_seed = *seed_ptr;
#ifdef MSL_SUBSYSTEM_PROFILE
    msl_profile_add(MSL_PROFILE_FINISH,
                         msl_profile_cycles() - finish_started);
#endif
    return 0;
}

int msl_core_match_step(MslCoreMatch* match, const MslCoreInput* input,
                        uint32_t frame_seed,
                        const MslCoreStageEvents* stage_events)
{
#ifdef MSL_SUBSYSTEM_PROFILE
    uint64_t started = msl_profile_cycles();
#endif
    if (msl_core_match_step_prepare(match, input, frame_seed, stage_events) !=
        0)
    {
        return -1;
    }
#ifdef MSL_SUBSYSTEM_PROFILE
    msl_profile_add(MSL_PROFILE_PREPARE,
                         msl_profile_cycles() - started);
    started = msl_profile_cycles();
#endif
    // Prepare has already published this Match's complete hosted context.
    // Run the source scheduler directly so the scalar canonical path does not
    // re-enter the cross-Match binding wrappers at every priority/proc seam.
    // refs/melee/src/sysdolphin/baselib/gobj.c::HSD_GObj_80390CFC
    HSD_GObj_80390CFC();
#ifdef MSL_SUBSYSTEM_PROFILE
    msl_profile_add(MSL_PROFILE_SCHEDULER,
                         msl_profile_cycles() - started);
#endif
    return msl_core_match_step_finish(match, frame_seed);
}

const MslCoreCompare* msl_core_match_output(const MslCoreMatch* match)
{
    msl_core_bind_match((MslCoreMatch*) match);
    write_compare(match, match->last_frame_seed,
                  &((MslCoreMatch*) match)->output);
    return &match->output;
}
