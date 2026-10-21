#include "effects.h"

#include "cm/camera.h"
#include "ef/efasync.h"
#include "ef/efsync.h"
#include "ef/types.h"
#include "gr/ground.h"
#include "lb/lb_00B0.h"
#include "lb/lbarchive.h"
#include "runtime/effect_pieces.h"
#include "runtime/viewer_track.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <baselib/gobj.h>
#include <baselib/gobjproc.h>
#include <baselib/jobj.h>
#include <baselib/psstructs.h>
#include <baselib/random.h>

#ifdef MSL_CORE_NATIVE
#include "platform/native_dat.h"
#endif

static _Thread_local const MslCoreEffectData* msl_bound_effect_data;
static _Thread_local MslCoreEffectState* msl_bound_effect_state;

#define msl_effect_banks (msl_bound_effect_data->banks)
#define msl_effect_nodes (msl_bound_effect_state->nodes)
#define msl_effect_free (msl_bound_effect_state->free)

static void msl_effect_consume_bank_command(
    const MslCoreEffectGeneratorBank* data, int index);
#if MSL_VISUAL_TRACK_BUILD

// The viewer track's random state counts the draws the game makes that the
// simulator leaves out (runtime/viewer_track.c): for those it needs every
// file of the game's own effect list (efAsync_DatEntries: slot, file, table),
// the ones gameplay's list leaves out loaded beside it, and each model's
// first animation step's draws. Loaded only by msl_core_game_data_init_visual;
// gameplay's banks and draws are untouched. (The menu's file, slot 31, is not
// a match's.)
// refs/melee/src/melee/ef/efasync.c::efAsync_DatEntries
enum { MSL_EFFECT_MODELS_PER_FILE = 16 };
// psInitDataBankLoad's slot for the stage archive's map_ptcl.
// refs/melee/src/melee/gr/ground.c::Ground_801C0800
enum { MSL_EFFECT_STAGE_BANK = 0x1E };
struct MslCoreEffectVisual {
    MslCoreEffectGeneratorBank view_banks[MSL_CORE_EFFECT_VISUAL_BANKS];
    EF_EffectDesc* models[MSL_CORE_EFFECT_VISUAL_BANKS];
    u8 model_draws[MSL_CORE_EFFECT_VISUAL_BANKS][MSL_EFFECT_MODELS_PER_FILE];
};
static const struct {
    int slot;
    const char* file;
    const char* table;
} msl_effect_game_files[] = {
    { 0, "/EfCoData.dat", "effCommonDataTable" }, { 1, "/EfMrData.dat", "effMarioDataTable" },
    { 2, "/EfSsData.dat", "effSamusDataTable" }, { 3, "/EfFxData.dat", "effFoxDataTable" },
    { 4, "/EfCaData.dat", "effCaptainDataTable" }, { 5, "/EfKbData.dat", "effKirbyDataTable" },
    { 6, "/EfLkData.dat", "effLinkDataTable" }, { 7, "/EfPkData.dat", "effPikachuDataTable" },
    { 8, "/EfDkData.dat", "effDonkeyDataTable" }, { 9, "/EfYsData.dat", "effYoshiDataTable" },
    { 10, "/EfNsData.dat", "effNessDataTable" }, { 11, "/EfPrData.dat", "effPurinDataTable" },
    { 12, "/EfKpData.dat", "effKoopaDataTable" }, { 13, "/EfMtData.dat", "effMewtwoDataTable" },
    { 14, "/EfIcData.dat", "effIceclimberDataTable" }, { 15, "/EfPeData.dat", "effPeachDataTable" },
    { 16, "/EfMsData.dat", "effMarsDataTable" }, { 17, "/EfZdData.dat", "effZeldaDataTable" },
    { 18, "/EfLgData.dat", "effLuigiDataTable" }, { 19, "/EfGnData.dat", "effGanonDataTable" },
    { 20, "/EfKbMs.dat", "effKirbyMarsDataTable" }, { 21, "/EfKbZd.dat", "effKirbyZeldaDataTable" },
    { 32, "/EfKbMr.dat", "effKirbyMarioDataTable" }, { 33, "/EfKbFx.dat", "effKirbyFoxDataTable" },
    { 34, "/EfKbSs.dat", "effKirbySamusDataTable" }, { 36, "/EfKbPk.dat", "effKirbyPikachuDataTable" },
    { 37, "/EfKbLg.dat", "effKirbyLuigiDataTable" }, { 38, "/EfKbCa.dat", "effKirbyCaptainDataTable" },
    { 39, "/EfKbDk.dat", "effKirbyDonkeyDataTable" }, { 41, "/EfKbKp.dat", "effKirbyKoopaDataTable" },
    { 46, "/EfKbIc.dat", "effKirbyIceDataTable" }, { 47, "/EfKbGn.dat", "effKirbyGanonDataTable" },
    { 48, "/EfKbFe.dat", "effKirbyEmblemDataTable" }, { 49, "/EfFeData.dat", "effEmblemDataTable" },
};
// How many entries each file's model table has (an audit of the files: 36,
// 41 and 46 have one entry with no model in it).
static const u8 msl_effect_model_counts[MSL_CORE_EFFECT_VISUAL_BANKS] = {
    [0] = 47, [1] = 2, [2] = 3, [3] = 6, [4] = 6, [5] = 9, [6] = 4, [7] = 5, [8] = 7,
    [9] = 1, [10] = 3, [11] = 1, [12] = 3, [13] = 4, [14] = 1, [15] = 1,
    [16] = 2, [17] = 7, [18] = 2, [19] = 6, [20] = 2, [21] = 2, [32] = 1,
    [33] = 1, [34] = 1, [36] = 1, [37] = 1, [38] = 2, [39] = 2, [41] = 1,
    [46] = 1, [47] = 2, [48] = 2, [49] = 2,
};
// Set while a model's first step is counted (msl_effect_game_data_init_visual).
static _Thread_local u8* msl_effect_recording_draws;
// Set while msl_effect_game_data_init_visual loads: the common file's table
// is translated whole (a table is translated once, at the first count asked).
static _Thread_local int msl_effect_loading_visual;
#endif // MSL_VISUAL_TRACK_BUILD

static void msl_effect_record_model_generator(int link_no, int bank,
                                              int gfx_id, HSD_JObj* jobj)
{
    MslCoreEffectData* data = (MslCoreEffectData*) msl_bound_effect_data;
    MslCoreEffectModelStart* start;
    int index;

    (void) link_no;
    (void) jobj;
#if MSL_VISUAL_TRACK_BUILD
    if (msl_effect_recording_draws != NULL) {
        if (msl_effect_generator_draws(bank, gfx_id)) {
            ++*msl_effect_recording_draws;
        }
        return;
    }
#endif // MSL_VISUAL_TRACK_BUILD
    if (data == NULL || data->recording_common_model < 0 ||
        data->recording_common_model >= MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY)
    {
        return;
    }
    index = data->recording_common_model;
    start = &data->common_model_start[index];
    if (start->count >= MSL_CORE_EFFECT_MODEL_START_CAPACITY) {
        fprintf(stderr, "common effect model %d has too many startup generators\n",
                index + 9);
        abort();
    }
    start->generator_ids[start->count++] = bank * 1000 + gfx_id;
}

static void msl_effect_record_model_start(MslCoreEffectData* data,
                                          int model_id)
{
    EF_EffectDesc* desc = &data->banks[0].models[model_id];
    HSD_JObj* jobj;

    if (desc->model_desc.joint == NULL) {
        return;
    }
    jobj = HSD_JObjLoadJoint(desc->model_desc.joint);
    if (jobj == NULL) {
        fprintf(stderr, "failed to load common effect model %d\n", model_id);
        abort();
    }
    HSD_JObjAddAnimAll(jobj, desc->model_desc.animjoint,
                       desc->model_desc.matanim_joint,
                       desc->model_desc.shapeanim_joint);
    HSD_JObjReqAnimAll(jobj, 0.0F);
    data->recording_common_model = (s8) model_id;
    HSD_JObjAnimAll(jobj);
    data->recording_common_model = -1;
    HSD_JObjRemoveAll(jobj);
}

static void msl_effect_load_bank(MslCoreEffectData* data, int bank,
                                 const char* filename,
                                 const char* symbol)
{
    MslCoreEffectGeneratorBank* effect_bank = &data->banks[bank];
#ifdef MSL_CORE_NATIVE
    effect_bank->archive = lbArchive_LoadArchive(filename);
    if (msl_native_effect_bank(effect_bank->archive, symbol,
                               &effect_bank->count, &effect_bank->id_base,
                               &effect_bank->commands) != 0)
    {
        fprintf(stderr, "%s has invalid %s command data\n", filename,
                symbol);
        abort();
    }
    if (bank == 0) {
        effect_bank->models =
            msl_native_effect_models(
                effect_bank->archive, symbol,
#if MSL_VISUAL_TRACK_BUILD
                msl_effect_loading_visual ? msl_effect_model_counts[0]
                                          : MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY);
#else // !MSL_VISUAL_TRACK_BUILD
                MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY);
#endif // MSL_VISUAL_TRACK_BUILD
        if (effect_bank->models == NULL) {
            fprintf(stderr, "%s has invalid common effect model data\n",
                    filename);
            abort();
        }
    }
#else
    void** table = NULL;
    int* command_bank;
    u16 version;

    effect_bank->archive =
        lbArchive_80016DBC(filename, &table, symbol, MSL_LBARCHIVE_END);
    if (table == NULL) {
        fprintf(stderr, "%s is missing %s command data\n", filename, symbol);
        abort();
    }
    if (table[0] == NULL && table[1] == NULL) {
        // A model-only effect archive carries no particle banks and retail
        // skips psInitDataBank entirely (EfDkData.dat is the supported-domain
        // case). Publish an empty command bank so generator lookups consume
        // nothing. refs/melee/src/melee/ef/efasync.c::efAsync_LoadSync
        effect_bank->command_bank = NULL;
        effect_bank->count = 0;
        effect_bank->commands = NULL;
        effect_bank->id_base = 0;
        return;
    }
    if (table[0] == NULL) {
        fprintf(stderr, "%s is missing %s command data\n", filename, symbol);
        abort();
    }
    command_bank = table[0];
    effect_bank->command_bank = command_bank;
    version = *(u16*) command_bank;
    switch (version) {
    case 0:
        effect_bank->count = command_bank[1];
        effect_bank->commands = (HSD_PSCmdList**) (command_bank + 2);
        effect_bank->id_base = 0;
        break;
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
        effect_bank->count = command_bank[1] + command_bank[2];
        effect_bank->commands =
            (HSD_PSCmdList**) (command_bank + 3);
        effect_bank->id_base = command_bank[1];
        break;
    default:
        fprintf(stderr, "%s has unsupported particle command version %x\n",
                filename, version);
        abort();
    }
    if (bank == 0) {
        // efAsync_LoadSync publishes &table[2] as the EF_EffectDesc array.
        // refs/melee/src/melee/ef/efasync.c::efAsync_LoadSync
        effect_bank->models = (EF_EffectDesc*) &table[2];
    }
#endif
}

void msl_effect_projection_bind(const MslCoreEffectData* data,
                                MslCoreEffectState* state)
{
    msl_bound_effect_data = data;
    msl_bound_effect_state = state;
}

void msl_effect_game_data_init(MslCoreEffectData* data)
{
    memset(data, 0, sizeof(*data));
    data->recording_common_model = -1;
    msl_effect_load_bank(data, 0, "/EfCoData.dat", "effCommonDataTable");
    // Mario and Dr. Mario share efAsync bank 1 (ftData_UnkBytePerCharacter
    // maps both kinds to 1).
    msl_effect_load_bank(data, 1, "/EfMrData.dat", "effMarioDataTable");
    // Samus owns efAsync bank 2 (ftData_UnkBytePerCharacter maps
    // FTKIND_SAMUS to 2).
    msl_effect_load_bank(data, 2, "/EfSsData.dat", "effSamusDataTable");
    msl_effect_load_bank(data, 3, "/EfFxData.dat", "effFoxDataTable");
    // Donkey Kong owns efAsync bank 8 (ftData_UnkBytePerCharacter maps
    // FTKIND_DONKEY to 8).
    msl_effect_load_bank(data, 8, "/EfDkData.dat", "effDonkeyDataTable");
    // Yoshi owns efAsync bank 9 (ftData_UnkBytePerCharacter maps
    // FTKIND_YOSHI to 9). Both Yoshi-reachable efSync generator ids
    // (0x4CD/0x4CE -> 0x2328) resolve into this bank.
    msl_effect_load_bank(data, 9, "/EfYsData.dat", "effYoshiDataTable");
    // Ness owns efAsync bank 10 (ftData_UnkBytePerCharacter maps FTKIND_NESS
    // to 10). EfNsData.dat is MODEL-ONLY -- both leading effNessDataTable
    // words are unrelocated NULLs, the same shape as EfDkData.dat -- so
    // retail efAsync_LoadSync skips psInitDataBank and the hosted loader
    // publishes an empty command bank. Every Ness-reachable efSync id
    // (0x4EE/0x4EF from PK Thunder, 0x4F0 from PSI Magnet) is a pure efLib
    // model create, so no generator RNG projection consumes bank-10 ids.
    // refs/melee/src/melee/ef/efasync.c::efAsync_LoadSync
    msl_effect_load_bank(data, 10, "/EfNsData.dat", "effNessDataTable");
    // Link and Young Link share efAsync bank 6 (ftData_UnkBytePerCharacter
    // maps FTKIND_LINK and FTKIND_CLINK to 6; there is no EfClData.dat).
    // EfLkData.dat is MODEL-ONLY -- both leading effLinkDataTable words are
    // unrelocated NULLs, the EfDkData.dat/EfNsData.dat shape -- so the hosted
    // loader publishes an empty command bank. Every Link-reachable efSync id
    // (0x4BB/0x4BC from the spin attack) is a pure efLib model create on
    // bank-6 model ids 0x1770..0x1773, and the item-side ids (0x448 arrow
    // sparkle, 0x41C/0x3F1 hookshot) resolve into the already-loaded common
    // bank 0, so no generator RNG projection consumes bank-6 ids.
    // refs/melee/src/melee/ef/efasync.c::{efAsync_DatEntries,efAsync_LoadSync}
    msl_effect_load_bank(data, 6, "/EfLkData.dat", "effLinkDataTable");
    msl_effect_load_bank(data, 13, "/EfMtData.dat", "effMewtwoDataTable");
    msl_effect_load_bank(data, 12, "/EfKpData.dat", "effKoopaDataTable");
    // Pikachu owns efAsync bank 7 (ftData_UnkBytePerCharacter maps
    // FTKIND_PIKACHU to 7; Pichu shares the bank in retail).
    msl_effect_load_bank(data, 7, "/EfPkData.dat", "effPikachuDataTable");
    // Popo and Nana share efAsync bank 14 (ftData_UnkBytePerCharacter maps
    // both kinds to 14).
    msl_effect_load_bank(data, 14, "/EfIcData.dat", "effIceclimberDataTable");
    msl_effect_load_bank(data, 18, "/EfLgData.dat", "effLuigiDataTable");
    // Kirby owns efAsync bank 5, and each copy ability loads its own bank
    // through ftKb_SpecialN_800EED50 (ftKb_Init_803CB46C maps fighter kinds
    // to banks 20..48; the kinds whose table entry is NULL load nothing).
    // refs/melee/src/melee/ef/efasync.c::efAsync_DatEntries
    // refs/melee/src/melee/ft/chara/ftKirby/ftkirby.c::ftKb_Init_803CB46C
    msl_effect_load_bank(data, 5, "/EfKbData.dat", "effKirbyDataTable");
    msl_effect_load_bank(data, 20, "/EfKbMs.dat", "effKirbyMarsDataTable");
    msl_effect_load_bank(data, 21, "/EfKbZd.dat", "effKirbyZeldaDataTable");
    msl_effect_load_bank(data, 32, "/EfKbMr.dat", "effKirbyMarioDataTable");
    msl_effect_load_bank(data, 33, "/EfKbFx.dat", "effKirbyFoxDataTable");
    msl_effect_load_bank(data, 34, "/EfKbSs.dat", "effKirbySamusDataTable");
    msl_effect_load_bank(data, 36, "/EfKbPk.dat", "effKirbyPikachuDataTable");
    msl_effect_load_bank(data, 37, "/EfKbLg.dat", "effKirbyLuigiDataTable");
    msl_effect_load_bank(data, 38, "/EfKbCa.dat", "effKirbyCaptainDataTable");
    msl_effect_load_bank(data, 39, "/EfKbDk.dat", "effKirbyDonkeyDataTable");
    msl_effect_load_bank(data, 41, "/EfKbKp.dat", "effKirbyKoopaDataTable");
    msl_effect_load_bank(data, 46, "/EfKbIc.dat", "effKirbyIceDataTable");
    msl_effect_load_bank(data, 47, "/EfKbGn.dat", "effKirbyGanonDataTable");
    msl_effect_load_bank(data, 48, "/EfKbFe.dat", "effKirbyEmblemDataTable");
    // efLib_Create queues each common model's frame-zero animation, and both
    // efAsync_Dispatch and efSync_Spawn drain that queue before returning.
    // Record its data-defined DPtcl generator events once while allocation is
    // legal; runtime replays only their RNG-bearing generator initialization
    // through the immutable catalog below.
    // refs/melee/src/melee/ef/{efasync.c::efAsync_Dispatch,
    //     efsync.c::efSync_Spawn,eflib.c::efLib_Cb_DPtcl}
    // refs/melee/src/sysdolphin/baselib/jobj.c::JObjUpdateFunc
    msl_effect_projection_bind(data, NULL);
    HSD_JObjSetDPtclCallback(msl_effect_record_model_generator);
    for (int model_id = 0;
         model_id < MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY; ++model_id)
    {
        msl_effect_record_model_start(data, model_id);
    }
    HSD_JObjSetDPtclCallback(NULL);
}
#if MSL_VISUAL_TRACK_BUILD

#ifdef MSL_CORE_NATIVE
int msl_effect_game_data_init_visual(MslCoreEffectData* data)
{
    MslCoreEffectVisual* visual;
    size_t k;

    msl_effect_loading_visual = 1;
    msl_effect_game_data_init(data);
    msl_effect_loading_visual = 0;
    visual = calloc(1, sizeof(*visual));
    if (visual == NULL) {
        return -1;
    }
    data->visual = visual;
    for (k = 0; k < sizeof(msl_effect_game_files) / sizeof(msl_effect_game_files[0]); ++k) {
        int slot = msl_effect_game_files[k].slot;
        HSD_Archive* archive;
        if (slot < MSL_CORE_EFFECT_BANK_CAPACITY && data->banks[slot].archive != NULL) {
            archive = data->banks[slot].archive;
        } else {
            MslCoreEffectGeneratorBank* view = &visual->view_banks[slot];
            view->archive = archive = lbArchive_LoadArchive(msl_effect_game_files[k].file);
            if (archive == NULL) {
                continue;
            }
            if (msl_native_effect_bank(archive, msl_effect_game_files[k].table, &view->count,
                                       &view->id_base, &view->commands) != 0)
            {
                view->count = 0;
                view->commands = NULL;
            }
        }
        if (msl_effect_model_counts[slot] != 0) {
            visual->models[slot] = msl_native_effect_models(
                archive, msl_effect_game_files[k].table, msl_effect_model_counts[slot]);
        }
    }
    msl_effect_projection_bind(data, NULL);
    HSD_JObjSetDPtclCallback(msl_effect_record_model_generator);
    for (int bank = 0; bank < MSL_CORE_EFFECT_VISUAL_BANKS; ++bank) {
        const EF_EffectDesc* table = visual->models[bank];
        // (the common file's first 0x28 are the simulator's own)
        int first = bank == 0 ? MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY : 0;
        for (int model = first; table != NULL && model < msl_effect_model_counts[bank] &&
                                model - first < MSL_EFFECT_MODELS_PER_FILE;
             ++model)
        {
            const EF_EffectDesc* desc = &table[model];
            HSD_JObj* jobj;
            if (desc->model_desc.joint == NULL ||
                (jobj = HSD_JObjLoadJoint(desc->model_desc.joint)) == NULL)
            {
                continue;
            }
            HSD_JObjAddAnimAll(jobj, desc->model_desc.animjoint, desc->model_desc.matanim_joint,
                               desc->model_desc.shapeanim_joint);
            HSD_JObjReqAnimAll(jobj, 0.0F);
            msl_effect_recording_draws = &visual->model_draws[bank][model - first];
            HSD_JObjAnimAll(jobj);
            msl_effect_recording_draws = NULL;
            HSD_JObjRemoveAll(jobj);
        }
    }
    HSD_JObjSetDPtclCallback(NULL);
    return 0;
}
#endif

void msl_effect_game_data_visual_deinit(MslCoreEffectData* data)
{
    free(data->visual);
    data->visual = NULL;
}

// A bank as the game's particle code finds it: the running stage's map_ptcl,
// a file gameplay does not load, or gameplay's own.
static const MslCoreEffectGeneratorBank* msl_effect_visual_bank(int bank)
{
    const MslCoreEffectVisual* visual =
        msl_bound_effect_data != NULL ? msl_bound_effect_data->visual : NULL;
    if (bank == MSL_EFFECT_STAGE_BANK) {
#ifdef MSL_CORE_NATIVE
        if (msl_bound_effect_data == NULL ||
            (u32) stage_info.internal_stage_id >=
                sizeof(msl_bound_effect_data->stage_banks) /
                    sizeof(msl_bound_effect_data->stage_banks[0]))
        {
            return NULL;
        }
        return &msl_bound_effect_data->stage_banks[stage_info.internal_stage_id];
#else
        return NULL;
#endif
    }
    if (visual != NULL && bank > 0 && bank < MSL_CORE_EFFECT_VISUAL_BANKS &&
        visual->view_banks[bank].archive != NULL)
    {
        return &visual->view_banks[bank];
    }
    if (msl_bound_effect_data == NULL || bank < 0 || bank >= MSL_CORE_EFFECT_BANK_CAPACITY) {
        return NULL;
    }
    return &msl_effect_banks[bank];
}

int msl_effect_view_only(int bank)
{
    const MslCoreEffectVisual* visual =
        msl_bound_effect_data != NULL ? msl_bound_effect_data->visual : NULL;
    return visual != NULL && bank > 0 && bank < MSL_CORE_EFFECT_VISUAL_BANKS &&
           visual->view_banks[bank].archive != NULL;
}

int msl_effect_generator_draws(int bank, int generator_id)
{
    const MslCoreEffectGeneratorBank* data = msl_effect_visual_bank(bank);
    const HSD_PSCmdList* command;

    // (indexed by the full generator id: hsd_8039F05C)
    if (data == NULL || data->commands == NULL || generator_id < data->id_base ||
        generator_id >= data->count)
    {
        return 0;
    }
    command = data->commands[generator_id - data->id_base];
    return command != NULL && !(command->kind & 0x100) && command->random >= 0.0F;
}

int msl_effect_model_first_draws(int model_id)
{
    const MslCoreEffectVisual* visual =
        msl_bound_effect_data != NULL ? msl_bound_effect_data->visual : NULL;
    int bank = model_id / 1000;
    int index = model_id % 1000;
    // (the common file's from 0x28 on, kept from 0)
    if (bank == 0) {
        index -= MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY;
    }
    if (visual == NULL || model_id < 0 || index < 0 || bank >= MSL_CORE_EFFECT_VISUAL_BANKS ||
        index >= MSL_EFFECT_MODELS_PER_FILE)
    {
        return 0;
    }
    return visual->model_draws[bank][index];
}
#endif // MSL_VISUAL_TRACK_BUILD

#ifndef MSL_CORE_NATIVE
// psInitDataBankLoad's header parse for an in-place command bank; entries
// stay bank-relative (msl_effect_consume_bank_command relocates them).
// refs/melee/src/sysdolphin/baselib/particle.c::psInitDataBankLoad
static int msl_effect_parse_command_bank(MslCoreEffectGeneratorBank* bank,
                                         int* command_bank)
{
    u16 version;

    memset(bank, 0, sizeof(*bank));
    if (command_bank == NULL) {
        return 0;
    }
    version = *(u16*) command_bank;
    bank->command_bank = command_bank;
    if (version == 0) {
        bank->count = command_bank[1];
        bank->commands = (HSD_PSCmdList**) (command_bank + 2);
    } else if (version >= 0x40 && version <= 0x43) {
        bank->count = command_bank[1] + command_bank[2];
        bank->commands = (HSD_PSCmdList**) (command_bank + 3);
        bank->id_base = command_bank[1];
    } else {
        return -1;
    }
    return 0;
}
#endif

#ifdef MSL_CORE_NATIVE
int msl_effect_load_stage_bank(MslCoreEffectData* data,
                               s32 internal_stage_id, HSD_Archive* archive)
{
    MslCoreEffectGeneratorBank* bank;

    if ((u32) internal_stage_id >= sizeof(data->stage_banks) /
                                      sizeof(data->stage_banks[0]))
    {
        return -1;
    }
    bank = &data->stage_banks[internal_stage_id];
    bank->archive = archive;
    return msl_native_particle_bank(archive, "map_ptcl", &bank->count,
                                    &bank->id_base, &bank->commands);
}
#endif

// efLib_Cb_DPtcl -> grLib_801C99C0 -> hsd_8039F05C: stage generators
// consume gameplay RNG during joint animation, before fighter CPU decisions.
static void msl_effect_match_dptcl(int link_no, int bank, int gfx_id,
                                   HSD_JObj* jobj)
{
#if MSL_VISUAL_TRACK_BUILD
    if (msl_viewer_track_on()) {
        msl_viewer_track_gen(bank, gfx_id, jobj);
#endif // MSL_VISUAL_TRACK_BUILD
#if MSL_VISUAL_TRACK_BUILD
        // (a stage's key is drawn for below; another file's is a draw the
        // simulator leaves out)
        if (bank != 0x1E) {
            msl_viewer_track_skipped(bank, gfx_id);
        }
#endif // MSL_VISUAL_TRACK_BUILD
#if MSL_VISUAL_TRACK_BUILD
    }
#endif // MSL_VISUAL_TRACK_BUILD
    if (bank != 0x1E || msl_bound_effect_state == NULL) {
        return;
    }
#ifdef MSL_CORE_NATIVE
    msl_effect_consume_bank_command(
        &msl_bound_effect_data->stage_banks[stage_info.internal_stage_id],
        gfx_id);
#else
    MslCoreEffectGeneratorBank data;
    if (msl_effect_parse_command_bank(&data, (int*) stage_info.map_ptcl) == 0) {
        msl_effect_consume_bank_command(&data, gfx_id);
    }
#endif
}

void msl_effect_install_match_callbacks(void)
{
    HSD_JObjSetDPtclCallback(msl_effect_match_dptcl);
}

void msl_effect_match_init(const MslCoreEffectData* data,
                           MslCoreEffectState* state)
{
    int i;

    memset(state, 0, sizeof(*state));
    msl_effect_projection_bind(data, state);
    msl_effect_free = NULL;
    for (i = MSL_CORE_EFFECT_QUEUE_CAPACITY - 1; i >= 0; --i) {
        msl_effect_nodes[i].next = msl_effect_free;
        msl_effect_free = &msl_effect_nodes[i];
    }
}

static MslCoreEffectQueueNode* msl_effect_alloc(void)
{
    MslCoreEffectQueueNode* node = msl_effect_free;
    if (node == NULL) {
        fprintf(stderr, "headless effect queue exhausted (%d nodes)\n",
                MSL_CORE_EFFECT_QUEUE_CAPACITY);
        abort();
    }
    msl_effect_free = node->next;
    memset(node, 0, sizeof(*node));
    return node;
}

u32 msl_effect_queue_used(void)
{
    u32 free_count = 0;
    const MslCoreEffectQueueNode* node;
    if (msl_bound_effect_state == NULL) {
        return 0;
    }
    for (node = msl_effect_free; node != NULL; node = node->next) {
        free_count += 1;
    }
    return (u32) MSL_CORE_EFFECT_QUEUE_CAPACITY - free_count;
}

void msl_effect_queue_dump(void)
{
    u8 is_free[MSL_CORE_EFFECT_QUEUE_CAPACITY] = {0};
    const MslCoreEffectQueueNode* node;
    int i;
    if (msl_bound_effect_state == NULL) {
        return;
    }
    for (node = msl_effect_free; node != NULL; node = node->next) {
        is_free[node - msl_effect_nodes] = 1;
    }
    for (i = 0; i < MSL_CORE_EFFECT_QUEUE_CAPACITY; ++i) {
        if (!is_free[i]) {
            fprintf(stderr,
                    "effect node %d checked out: spawn_kind=%u gfx_id=0x%x\n",
                    i, msl_effect_nodes[i].spawn_kind,
                    (unsigned) msl_effect_nodes[i].gfx_id);
        }
    }
}

static void msl_effect_release(MslCoreEffectQueueNode* node)
{
    node->next = msl_effect_free;
    msl_effect_free = node;
}

static void msl_effect_consume_generator_rng(s32 generator_id)
{
    int bank = generator_id / 1000;
    // hsd_8039EFAC(linkNo, bank, id) indexes ptclref[bank] by the full
    // generator id (Mario's fireball: bank 1, 0x3E9), and a version-0x4x bank
    // lists [id_base, count) at those ids. The id modulo 1000 is below
    // id_base for every such bank and found nothing.
    // refs/melee/src/sysdolphin/baselib/generator.c::hsd_8039F05C
    int index = generator_id;

    if (bank < 0 || bank >= (int) (sizeof(msl_effect_banks) /
                                   sizeof(msl_effect_banks[0])))
    {
        return;
    }
    msl_effect_consume_bank_command(&msl_effect_banks[bank], index);
}

// hsd_8039F05C's gate for one generator of a bank: a generator whose command
// list is not a static kind and has a nonnegative random spread draws once.
static void msl_effect_consume_bank_command(
    const MslCoreEffectGeneratorBank* data, int index)
{
    HSD_PSCmdList* command;

    // Only [id_base, count) is present in the immutable command array.
    if (index < data->id_base || index >= data->count ||
        data->commands == NULL)
    {
        return;
    }
    // Particle command entries are bank-relative offsets, not ordinary DAT
    // relocations. psInitDataBankLocate adds the command-bank base in place;
    // keep the immutable archive bytes and perform the same relocation here.
    // refs/melee/src/sysdolphin/baselib/particle.c::psInitDataBankLocate
    command = data->commands[index - data->id_base];
#ifndef MSL_CORE_NATIVE
    if (command != NULL) {
        command = (HSD_PSCmdList*) ((u8*) data->command_bank + (u32) command);
    }
#endif
    if (command != NULL && !(command->kind & 0x100) && command->random >= 0.0F)
    {
        // refs/melee/src/sysdolphin/baselib/particle.c::hsd_8039F05C
        (void) HSD_Randf();
    }
}

static void msl_effect_consume_common_model_start(int model_id)
{
    const MslCoreEffectModelStart* start;
    int i;

    if (model_id < 0 ||
        model_id >= MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY)
    {
        return;
    }
    start = &msl_bound_effect_data->common_model_start[model_id];
    for (i = 0; i < start->count; ++i) {
        msl_effect_consume_generator_rng(start->generator_ids[i]);
    }
}

static void msl_effect_consume_common_dispatch_generator(s32 gfx_id)
{
    s32 generator_id = -1;

    // Direct projection of the generator-producing cases in
    // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch. Model-backed
    // effects do not touch HSD RNG during creation and are absent headlessly.
    switch (gfx_id) {
    case 0x3E9: generator_id = 0xC; break;
    case 0x3EA: generator_id = 0x14; break;
    case 0x3ED:
        msl_effect_consume_generator_rng(0x50);
        generator_id = 0x54;
        break;
    case 0x3EF:
    case 0x3F0: generator_id = 0x42; break;
    case 0x3F1:
    case 0x3F2: generator_id = 0x14B; break;
    case 0x3F3: generator_id = 0xB; break;
    case 0x3F4: generator_id = 0x48; break;
    case 0x3FE: generator_id = 0x107; break;
    case 0x400:
    case 0x401: generator_id = 0x5A; break;
    case 0x402: generator_id = 0x59; break;
    case 0x403: generator_id = 0x5E; break;
    case 0x405: generator_id = 0x2C; break;
    case 0x407: generator_id = 0x3C; break;
    case 0x408: generator_id = 0x3E; break;
    case 0x409: generator_id = 0xE2; break;
    case 0x40A: generator_id = 0x241; break;
    case 0x40B: generator_id = 0x242; break;
    case 0x40C:
    case 0x40D: generator_id = 0x19; break;
    case 0x40E: generator_id = 0x43; break;
    case 0x40F: generator_id = 0xE3; break;
    case 0x410: generator_id = 0x22A; break;
    case 0x411: generator_id = 0x4B; break;
    case 0x412: generator_id = 0x13; break;
    case 0x413: generator_id = 0x37; break;
    case 0x414: generator_id = 0xE1; break;
    case 0x416: generator_id = 0x196; break;
    case 0x41B: generator_id = 0x31; break;
    case 0x41C: generator_id = 0x5D; break;
    case 0x41E: generator_id = 0x55; break;
    case 0x41F: generator_id = 0x5C; break;
    case 0x420: generator_id = 0x159; break;
    case 0x421: generator_id = 0x3F; break;
    case 0x422: generator_id = 0x5B; break;
    case 0x425: generator_id = 0x7E; break;
    case 0x426: generator_id = 0x7F; break;
    case 0x428:
    case 0x43F: generator_id = 0xCA; break;
    case 0x429: generator_id = 0xCE; break;
    case 0x42A: generator_id = 0xCF; break;
    case 0x42D: generator_id = 0x121; break;
    case 0x42E: generator_id = 0x13C; break;
    case 0x430: generator_id = 0x140; break;
    case 0x432: generator_id = 0x145; break;
    case 0x433: generator_id = 0x115; break;
    case 0x434: generator_id = 0x14D; break;
    case 0x435: generator_id = 0x14E; break;
    case 0x436: generator_id = 0x153; break;
    case 0x437: generator_id = 0x156; break;
    case 0x43A: generator_id = 0x193; break;
    case 0x43B: generator_id = 0x192; break;
    case 0x43C: generator_id = 0x1A0; break;
    case 0x43D: generator_id = 0x1AF; break;
    case 0x440: generator_id = 0x1D8; break;
    case 0x441: generator_id = 0x1FB; break;
    case 0x442: generator_id = 0x1DC; break;
    case 0x443: generator_id = 0x1F1; break;
    case 0x444: generator_id = 0x1FF; break;
    case 0x445: generator_id = 0x209; break;
    case 0x446: generator_id = 0x1B; break;
    case 0x448: generator_id = 0x92; break;
    case 0x44A:
    case 0x44B:
    case 0x44C: generator_id = 0x237; break;
    case 0x44D: generator_id = 0x48; break;
    case 0x44E:
    case 0x457: generator_id = 0xDA; break;
    case 0x44F:
    case 0x458: generator_id = 0xDB; break;
    case 0x450:
    case 0x459: generator_id = 0xDC; break;
    case 0x451:
    case 0x45A: generator_id = 0xDD; break;
    case 0x452: generator_id = 0x21E; break;
    case 0x453: generator_id = 0x234; break;
    case 0x454: generator_id = 0x235; break;
    case 0x455: generator_id = 0x236; break;
    case 0x456: generator_id = 0x23D; break;
    case 0x45B: generator_id = 0x8C; break;
    case 0x45C: generator_id = 0x8D; break;
    case 0x45D: generator_id = 0x23F; break;
    case 0x45E: generator_id = 0x240; break;
    case 0x45F: generator_id = 0x8E; break;
    case 0x460: generator_id = 0x99; break;
    case 0x461: generator_id = 0x95; break;
    case 0x462: generator_id = 0x219; break;
    case 0x465: generator_id = 0x88; break;
    case 0x466: generator_id = 0x89; break;
    case 0x467: generator_id = 0x8A; break;
    case 0x46A: generator_id = 0xAC; break;
    case 0x46C: generator_id = 0x9E; break;
    case 0x46E: generator_id = 0xAE; break;
    case 0x46F: generator_id = 0xA0; break;
    case 0x470: generator_id = 0x21B; break;
    case 0x471: generator_id = 0x220; break;
    case 0x472: generator_id = 0x131; break;
    case 0x473: generator_id = 0x82; break;
    case 0x474: generator_id = 0xF7; break;
    case 0x475: generator_id = 0xFC; break;
    case 0x476: generator_id = 0xFF; break;
    case 0x477: generator_id = 0x7918; break;
    }
    if (generator_id >= 0) {
        msl_effect_consume_generator_rng(generator_id);
    }
}

#if MSL_VISUAL_TRACK_BUILD
// The random numbers a real creation of this generator, or of this common
// model, draws (runtime/effect_pieces.c).
void msl_effect_pieces_consume_generator(s32 generator_id)
{
    msl_effect_consume_generator_rng(generator_id);
}

void msl_effect_pieces_consume_model(int model_id)
{
    msl_effect_consume_common_model_start(model_id);
}

#endif // MSL_VISUAL_TRACK_BUILD
#if MSL_VISUAL_TRACK_BUILD
// A vararg that should be a pointer, or NULL when it cannot be one. A float
// passed by value arrives as a double whose bits are far above user-space
// addresses; NULL and misaligned values are out.
static const void* msl_effect_plausible(void* p)
{
    uintptr_t v = (uintptr_t) p;
    if (v < 0x10000U || (v & 3U) != 0) {
        return NULL;
    }
#if UINTPTR_MAX > 0xFFFFFFFFU
    if (v >= ((uintptr_t) 1 << 47)) {
        return NULL;
    }
#endif
    return p;
}

// A float vararg slot. Retail passes f32*; some decomp call sites pass the
// float itself, which arrives promoted to double. A plausible pointer is read
// through; otherwise the slot is taken as a double when that gives a finite,
// reasonably sized value.
static float msl_effect_float_arg(uint64_t bits)
{
    const void* p = msl_effect_plausible((void*) (uintptr_t) bits);
    double d;
    if (p != NULL) {
        return *(const f32*) p;
    }
    memcpy(&d, &bits, sizeof(d));
    if (d == d && d > -1.0e7 && d < 1.0e7) {
        return (float) d;
    }
    return 0.0F;
}

#if UINTPTR_MAX == 0xFFFFFFFFU
// 32-bit hosts (wasm32): a pointer is a 4-byte vararg slot and a promoted
// float an 8-byte one, so the slot is looked at as a pointer first and taken
// as one only when it is plausible; otherwise it is read as the double. A
// float promoted to double has its low 29 bits clear, so a "pointer" whose
// low 29 bits are clear is the double's low word, not a pointer.
static float msl_effect_float_vararg(va_list* args)
{
    va_list peek;
    void* p;
    double d;
    va_copy(peek, *args);
    p = va_arg(peek, void*);
    va_end(peek);
    if (msl_effect_plausible(p) != NULL && ((uintptr_t) p & 0x1FFFFFFFU) != 0
#ifdef __wasm__
        && (uintptr_t) p < (uintptr_t) __builtin_wasm_memory_size(0) * 65536U
#endif
    ) {
        (void) va_arg(*args, void*);
        return *(const f32*) p;
    }
    d = va_arg(*args, double);
    return d == d && d > -1.0e7 && d < 1.0e7 ? (float) d : 0.0F;
}
#define MSL_EFFECT_FLOAT_VARARG(args) msl_effect_float_vararg(&(args))
#else
#define MSL_EFFECT_FLOAT_VARARG(args) msl_effect_float_arg(va_arg(args, uint64_t))
#endif

// Set while a queued efAsync spawn is processed: its "fx" was written when
// it was queued.
static _Thread_local int msl_effect_processing_queued;

#endif // MSL_VISUAL_TRACK_BUILD
void* efSync_Spawn(s32 gfx_id, HSD_GObj* gobj, ...)
{
#if MSL_VISUAL_TRACK_BUILD
    if (msl_viewer_track_on() && !msl_effect_processing_queued) {
        va_list args;
        void* first;
        va_start(args, gobj);
        first = va_arg(args, void*);
        va_end(args);
        // Generators take a position (efLib_CreateGenerator); the other
        // dispatches take the joint they attach to first.
        if (gfx_id < 0x250 || gfx_id / 1000 == 0x1E) {
            msl_viewer_track_fx(gobj, gfx_id, NULL, msl_effect_plausible(first),
                                0.0F, 0.0F);
        } else {
            msl_viewer_track_fx(gobj, gfx_id, (HSD_JObj*) first, NULL, 0.0F,
                                0.0F);
        }
#else // !MSL_VISUAL_TRACK_BUILD
    (void) gobj;
#endif // MSL_VISUAL_TRACK_BUILD
#if MSL_VISUAL_TRACK_BUILD
        va_start(args, gobj);
        msl_effect_pieces(gfx_id, gobj, args);
        va_end(args);
#endif // MSL_VISUAL_TRACK_BUILD
#if MSL_VISUAL_TRACK_BUILD
    }
#endif // MSL_VISUAL_TRACK_BUILD
    // Visual objects are excluded, but immediate generator initialization and
    // explicit dispatch randomness share the gameplay HSD stream. The lookup
    // above reads `kind` and `random` directly from EfCoData/EfFxData and
    // applies hsd_8039F05C's source gate.
    // refs/melee/src/melee/ef/{efsync.c,efasync.c,efalt.c}
    // refs/melee/src/sysdolphin/baselib/particle.c::hsd_8039F05C
    if (gfx_id < 0x250 || gfx_id / 1000 == 0x1E) {
        msl_effect_consume_generator_rng(gfx_id);
    } else if (gfx_id < 0x478) {
        msl_effect_consume_common_dispatch_generator(gfx_id);
    } else if (gfx_id == 0x48D) {
        // refs/melee/src/melee/ef/efalt.c::efAlt_Spawn Fox reflector pulse.
        msl_effect_consume_generator_rng(0xBC0);
    } else {
        switch (gfx_id) {
        // Mario/Dr. Mario efAlt cases: fireball spawn pairs a model effect
        // with bank-1 generator 0x3E9; the fireball trail (0x47B) and both
        // cape flourishes (0x47D/0x47E) are generators alone. The tornado
        // (0x47C) is model-backed only. Doc's pill sparkle (0x4A0) resolves
        // to generator 0x7D02 in efAsync bank 32 (EfKbMr.dat), which is not
        // loaded outside Kirby-hat matches, so it consumes nothing here.
        // refs/melee/src/melee/ef/efalt.c
        case 0x47A: msl_effect_consume_generator_rng(0x3E9); break;
        case 0x47B: msl_effect_consume_generator_rng(0x3EB); break;
        case 0x47D: msl_effect_consume_generator_rng(0x3F0); break;
        case 0x47E: msl_effect_consume_generator_rng(0x3F1); break;
        case 0x4A0: msl_effect_consume_generator_rng(0x7D02); break;
        // Kirby efAlt cases (bank 5 and the copy banks): generator-backed
        // spawns draw their dispatcher gate; the model creations (0x494..
        // 0x499, 0x49D, 0x4A4, 0x4A9..0x4AC, 0x4B3..0x4B9) consume nothing.
        // 0x49A inhale wind, 0x49B/0x49C Stone transform and impact, 0x49E
        // spit star, 0x49F Mario copy fireball, 0x4A1..0x4A3 Samus copy
        // charge, 0x4A5..0x4A8 Bowser copy breath, 0x4AD Pikachu copy jolt,
        // 0x4AE..0x4B0 Ice Climbers copy shot, 0x4B1/0x4B2 Luigi copy
        // fireball. refs/melee/src/melee/ef/efalt.c::efAlt_Spawn
        case 0x49A: msl_effect_consume_generator_rng(0x138B); break;
        case 0x49B: msl_effect_consume_generator_rng(0x138F); break;
        case 0x49C: msl_effect_consume_generator_rng(0x1395); break;
        case 0x49E: msl_effect_consume_generator_rng(0x206); break;
        case 0x49F: msl_effect_consume_generator_rng(0x7D00); break;
        case 0x4A1: msl_effect_consume_generator_rng(0x84D4); break;
        case 0x4A2: msl_effect_consume_generator_rng(0x84D2); break;
        case 0x4A3: msl_effect_consume_generator_rng(0x84D3); break;
        case 0x4A5: msl_effect_consume_generator_rng(0xA028); break;
        case 0x4A6: msl_effect_consume_generator_rng(0xA029); break;
        case 0x4A7: msl_effect_consume_generator_rng(0xA02A); break;
        case 0x4A8: msl_effect_consume_generator_rng(0xA02B); break;
        case 0x4AD: msl_effect_consume_generator_rng(0x8CA0); break;
        case 0x4AE: msl_effect_consume_generator_rng(0xB3B0); break;
        case 0x4AF: msl_effect_consume_generator_rng(0xB3B1); break;
        case 0x4B0: msl_effect_consume_generator_rng(0xB3B6); break;
        case 0x4B1: msl_effect_consume_generator_rng(0x9088); break;
        case 0x4B2: msl_effect_consume_generator_rng(0x908A); break;
        case 0x4BD: msl_effect_consume_generator_rng(0x1B58); break;
        case 0x4BE: msl_effect_consume_generator_rng(0x1B5C); break;
        case 0x4BF:
            msl_effect_consume_generator_rng(0x1B5D);
            msl_effect_consume_generator_rng(0x5F);
            break;
        case 0x4C3: msl_effect_consume_generator_rng(0x24C); break;
        case 0x4CD:
        case 0x4CE: msl_effect_consume_generator_rng(0x2328); break;
        case 0x4D1: msl_effect_consume_generator_rng(0x64); break;
        case 0x4D3:
            msl_effect_consume_generator_rng(0x172);
            msl_effect_consume_generator_rng(0x173);
            break;
        case 0x4D4: msl_effect_consume_generator_rng(0x11E); break;
        case 0x4D8: msl_effect_consume_generator_rng(0x61); break;
        case 0x4DF: msl_effect_consume_generator_rng(0x143); break;
        case 0x4E3: msl_effect_consume_generator_rng(0x18A); break;
        case 0x4E4: msl_effect_consume_generator_rng(0x194); break;
        case 0x4E5: msl_effect_consume_generator_rng(0x17D); break;
        case 0x4E6: msl_effect_consume_generator_rng(0x17E); break;
        case 0x4E7: msl_effect_consume_generator_rng(0x196); break;
        case 0x4F8: msl_effect_consume_generator_rng(0x6E); break;
        case 0x4F9: msl_effect_consume_generator_rng(0x1C8); break;
        case 0x4FA: msl_effect_consume_generator_rng(0x166); break;
        case 0x4FB: msl_effect_consume_generator_rng(0x71); break;
        case 0x502: msl_effect_consume_generator_rng(0x1A6); break;
        case 0x503: msl_effect_consume_generator_rng(0x6A); break;
        case 0x504: msl_effect_consume_generator_rng(0x6D); break;
        case 0x505: msl_effect_consume_generator_rng(0x79); break;
        // Luigi fireball spawn pairs a model effect with generator 0x4650;
        // the fizzle-out is generator 0x4652 alone. The cyclone (0x509) is
        // model-backed only. refs/melee/src/melee/ef/efsync.c
        case 0x507: msl_effect_consume_generator_rng(0x4650); break;
        case 0x508: msl_effect_consume_generator_rng(0x4652); break;
        case 0x50A: msl_effect_consume_generator_rng(0x5F); break;
        }
    }

    switch (gfx_id) {
    case 0x3E8: {
        int model_id = HSD_Randi(8) == 0 ? 9 : 10;
        msl_effect_consume_common_model_start(model_id);
        break;
    }
    case 0x3EB:
        msl_effect_consume_common_model_start(7);
        break;
    case 0x3EC:
        msl_effect_consume_common_model_start(8);
        (void) HSD_Randf();
        break;
    case 0x3EE:
        msl_effect_consume_common_model_start(0x27);
        (void) HSD_Randf();
        break;
    // The smoke/impact puff family dispatches straight model creations
    // (efLib_Create_Attach_Pos(gfx - 0x3E5)) with no dispatcher randomness;
    // their frame-zero DPtcl events still initialize RNG-bearing generators.
    // Popo's damage-animation GFXSpawn 0x3F8 sits one draw ahead of the
    // DamageFlyRoll roll on the bf-marth/medium-marth launches.
    // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch
    case 0x3F5:
    case 0x3F6:
    case 0x3F7:
    case 0x3F8:
    case 0x3F9:
    case 0x3FA:
    case 0x3FB:
    case 0x3FC:
        msl_effect_consume_common_model_start(gfx_id - 0x3E5);
        break;
    // Large fire impact: efLib_Create_Attach_Pos(3, ...) whose frame-zero
    // DPtcl events initialize the explosion's generator fan-out (observed as
    // the retail 60-draw block ahead of the DamageFlyRoll roll on Mario
    // f-smash hits). refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch
    case 0x3FD:
        msl_effect_consume_common_model_start(3);
        break;
    // Yoshi ground-pound landing dust and Egg Roll wall bump: straight
    // model creations (efLib_Create_Attach_Pos(5) and (4)) with no
    // dispatcher randomness; their frame-zero DPtcl events still initialize
    // RNG-bearing generators.
    // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch
    case 0x3FF:
        msl_effect_consume_common_model_start(5);
        break;
    case 0x406:
        msl_effect_consume_common_model_start(4);
        break;
    case 0x427: {
        int i;
        for (i = 0; i < 6; ++i) {
            (void) HSD_Randf();
            (void) HSD_Randf();
        }
        break;
    }
    case 0x4CF:
    case 0x4D0: {
        int i;
        for (i = 0; i < 12; ++i) {
            (void) HSD_Randf();
            (void) HSD_Randf();
            (void) HSD_Randf();
        }
        break;
    }
    }
    return NULL;
}

static void msl_effect_process(HSD_GObj* gobj, MslCoreEffectQueueNode* node)
{
    if (node->spawn_kind == EF_SPAWN_CAMERA_SHAKE) {
        Vec3 position;
        lb_8000B1CC(node->jobj, &node->params, &position);
        Camera_80030E44(node->gfx_id, &position);
#if MSL_VISUAL_TRACK_BUILD
    } else if (msl_viewer_track_on()) {
#else // !MSL_VISUAL_TRACK_BUILD
    } else {
        efSync_Spawn(node->gfx_id, gobj);
    }
#endif // MSL_VISUAL_TRACK_BUILD
#if MSL_VISUAL_TRACK_BUILD
        const float* args =
            msl_viewer_track_queued_args((int) (node - msl_effect_nodes));
        if (args != NULL) {
            Vec3 params = { args[0], args[1], args[2] };
            msl_effect_pieces_deferred(gobj, node->gfx_id, node->spawn_kind,
                                       node->jobj, &params, args[3], args[4]);
        }
#endif // MSL_VISUAL_TRACK_BUILD
#if MSL_VISUAL_TRACK_BUILD
        msl_effect_processing_queued = 1;
        efSync_Spawn(node->gfx_id, gobj);
        msl_effect_processing_queued = 0;
    } else {
        efSync_Spawn(node->gfx_id, gobj);
    }
#endif // MSL_VISUAL_TRACK_BUILD
}

void efAsync_QueueFlush(HSD_GObj* gobj, void* queue_head)
{
    MslCoreEffectQueueNode* node = ((MslCoreEffectQueueNode*) queue_head)->next;
    while (node != NULL) {
        MslCoreEffectQueueNode* next = node->next;
        msl_effect_process(gobj, node);
        msl_effect_release(node);
        node = next;
    }
    ((MslCoreEffectQueueNode*) queue_head)->next = NULL;
}

void efAsync_QueueClear(void* queue_head)
{
    MslCoreEffectQueueNode* node = ((MslCoreEffectQueueNode*) queue_head)->next;
    while (node != NULL) {
        MslCoreEffectQueueNode* next = node->next;
        msl_effect_release(node);
        node = next;
    }
    ((MslCoreEffectQueueNode*) queue_head)->next = NULL;
}

void efAsync_Spawn(HSD_GObj* gobj, void* queue_head, u32 spawn_kind,
                   u32 gfx_id, HSD_JObj* jobj, ...)
{
    MslCoreEffectQueueNode* node = msl_effect_alloc();

    node->spawn_kind = spawn_kind;
    node->gfx_id = gfx_id;
    node->jobj = jobj;
#if MSL_VISUAL_TRACK_BUILD
    if (msl_viewer_track_on()) {
        // The arguments as efasync.c::efAsync_Spawn reads them per kind.
        // Some decomp call sites pass floats (promoted to double) where
        // retail passes f32*; msl_effect_float_arg takes either. A Vec3 the
        // call passes by value arrives by reference on this ABI.
        va_list args;
        const Vec3* at = NULL;
        float a0 = 0.0F;
        float a1 = 0.0F;
        va_start(args, jobj);
        switch (spawn_kind) {
        case EF_SPAWN_POS_OFFSET:
        case EF_SPAWN_ATTACH_OFFSET:
        case EF_SPAWN_CAMERA_SHAKE:
            at = msl_effect_plausible(va_arg(args, void*));
            break;
        case EF_SPAWN_ATTACH_PARAM:
        case EF_SPAWN_POS_PARAM:
            a0 = MSL_EFFECT_FLOAT_VARARG(args);
            break;
        case EF_SPAWN_POS_OFFSET_PARAM:
            at = msl_effect_plausible(va_arg(args, void*));
            a0 = MSL_EFFECT_FLOAT_VARARG(args);
            break;
        case EF_SPAWN_POS_OFFSET_PARAM2:
            at = msl_effect_plausible(va_arg(args, void*));
            a0 = MSL_EFFECT_FLOAT_VARARG(args);
            a1 = MSL_EFFECT_FLOAT_VARARG(args);
            break;
        default:
            break;
        }
        va_end(args);
        msl_viewer_track_fx(gobj, (s32) gfx_id, jobj, at, a0, a1);
#endif // MSL_VISUAL_TRACK_BUILD
#if MSL_VISUAL_TRACK_BUILD
        {
            // (kept for the pieces the spawn makes when it is processed)
            float* kept =
                msl_viewer_track_queued_args((int) (node - msl_effect_nodes));
            if (kept != NULL) {
                kept[0] = at != NULL ? at->x : 0.0F;
                kept[1] = at != NULL ? at->y : 0.0F;
                kept[2] = at != NULL ? at->z : 0.0F;
                if (spawn_kind == EF_SPAWN_ATTACH_PARAM ||
                    spawn_kind == EF_SPAWN_POS_PARAM)
                {
                    kept[0] = a0;
                }
                kept[3] = a0;
                kept[4] = a1;
            }
        }
#endif // MSL_VISUAL_TRACK_BUILD
#if MSL_VISUAL_TRACK_BUILD
    }
#endif // MSL_VISUAL_TRACK_BUILD
    if (spawn_kind == EF_SPAWN_CAMERA_SHAKE) {
        va_list args;
        va_start(args, jobj);
        node->params = *va_arg(args, Vec3*);
        va_end(args);
    }

    // Source inserts at the head while a lower-priority GObj process is
    // running, then Fighter's queue owner flushes in that reverse order.
    // refs/melee/src/melee/ef/efasync.c::{efAsync_Spawn,efAsync_QueueFlush}
    if (HSD_GObj_804D7838 != NULL && HSD_GObj_804D7838->s_link < 9U) {
        node->next = ((MslCoreEffectQueueNode*) queue_head)->next;
        ((MslCoreEffectQueueNode*) queue_head)->next = node;
    } else {
        msl_effect_process(gobj, node);
        msl_effect_release(node);
    }
}
