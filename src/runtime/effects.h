#ifndef MSL_CORE_RUNTIME_EFFECTS_H
#define MSL_CORE_RUNTIME_EFFECTS_H

#include <platform.h>

#include <dolphin/mtx.h>

#include <baselib/forward.h>
#include <baselib/psstructs.h>

#include "ef/forward.h"
#include "gr/forward.h"

enum {
    // Banks index refs/melee/src/melee/ef/efasync.c::efAsync_DatEntries;
    // Luigi's effLuigiDataTable occupies slot 18.
    // Kirby's copy-ability banks reach id 48 (EfKbFe.dat).
    // refs/melee/src/melee/ef/efasync.c::efAsync_DatEntries
    MSL_CORE_EFFECT_BANK_CAPACITY = 49,
    MSL_CORE_EFFECT_QUEUE_CAPACITY = 256,
    MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY = 0x28,
    MSL_CORE_EFFECT_MODEL_START_CAPACITY = 4,
#if MSL_VISUAL_TRACK_BUILD
    // Every slot of efAsync_DatEntries a match can load (the viewer track's).
    MSL_CORE_EFFECT_VISUAL_BANKS = 50,
#endif // MSL_VISUAL_TRACK_BUILD
};
#if MSL_VISUAL_TRACK_BUILD

// The viewer track's effect data (runtime/effects.c), NULL unless the game
// data was loaded by msl_core_game_data_init_visual.
typedef struct MslCoreEffectVisual MslCoreEffectVisual;
#endif // MSL_VISUAL_TRACK_BUILD

typedef struct MslCoreEffectModelStart {
    s32 generator_ids[MSL_CORE_EFFECT_MODEL_START_CAPACITY];
    u8 count;
} MslCoreEffectModelStart;

typedef struct MslCoreEffectQueueNode {
    struct MslCoreEffectQueueNode* next;
    u8 spawn_kind;
    s32 gfx_id;
    HSD_JObj* jobj;
    Vec3 params;
} MslCoreEffectQueueNode;

typedef struct MslCoreEffectGeneratorBank {
    HSD_Archive* archive;
    int* command_bank;
    HSD_PSCmdList** commands;
    EF_EffectDesc* models;
    s32 count;
    // Version-0x4x command banks index entries by the full generator id: the
    // occupied range [id_base, count) is stored at commands[id - id_base].
    // Version-0 banks have id_base 0.
    s32 id_base;
} MslCoreEffectGeneratorBank;

typedef struct MslCoreEffectData {
    MslCoreEffectGeneratorBank banks[MSL_CORE_EFFECT_BANK_CAPACITY];
#ifdef MSL_CORE_NATIVE
    MslCoreEffectGeneratorBank stage_banks[LAST + 1];
#endif
    MslCoreEffectModelStart
        common_model_start[MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY];
    s8 recording_common_model;
#if MSL_VISUAL_TRACK_BUILD
    MslCoreEffectVisual* visual;
#endif // MSL_VISUAL_TRACK_BUILD
} MslCoreEffectData;

typedef struct MslCoreEffectState {
    MslCoreEffectQueueNode nodes[MSL_CORE_EFFECT_QUEUE_CAPACITY];
    MslCoreEffectQueueNode* free;
} MslCoreEffectState;

void msl_effect_game_data_init(MslCoreEffectData* data);
#if MSL_VISUAL_TRACK_BUILD
#ifdef MSL_CORE_NATIVE
// msl_effect_game_data_init, and the viewer track's effect data.
int msl_effect_game_data_init_visual(MslCoreEffectData* data);
#endif
void msl_effect_game_data_visual_deinit(MslCoreEffectData* data);
// Whether making this generator draws a random number in the game
// (hsd_8039F05C: its command counts at random).
int msl_effect_generator_draws(int bank, int generator_id);
// Whether an effect file is one gameplay does not load.
int msl_effect_view_only(int bank);
// An effect model (file * 1000 + its place in the file's table) outside the
// common file's first 0x28: how many random numbers its animation's first
// step draws in the game.
int msl_effect_model_first_draws(int model_id);
#endif // MSL_VISUAL_TRACK_BUILD
#ifdef MSL_CORE_NATIVE
// Load immutable map_ptcl commands before GameData seals.
int msl_effect_load_stage_bank(MslCoreEffectData* data,
                               s32 internal_stage_id, HSD_Archive* archive);
#endif
// Installs the match-time joint-animation particle callback.
void msl_effect_install_match_callbacks(void);
void msl_effect_match_init(const MslCoreEffectData* data,
                           MslCoreEffectState* state);
void msl_effect_projection_bind(const MslCoreEffectData* data,
                                MslCoreEffectState* state);
// Checked-out nodes; must be zero after a complete frame.
u32 msl_effect_queue_used(void);
// Print checked-out nodes' spawn kinds and effect ids.
void msl_effect_queue_dump(void);

#endif
