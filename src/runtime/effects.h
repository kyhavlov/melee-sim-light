#ifndef MSL_CORE_RUNTIME_EFFECTS_H
#define MSL_CORE_RUNTIME_EFFECTS_H

#include <platform.h>

#include <dolphin/mtx.h>

#include <baselib/forward.h>
#include <baselib/psstructs.h>

#include "ef/forward.h"

enum {
    // Banks index refs/melee/src/melee/ef/efasync.c::efAsync_DatEntries;
    // Luigi's effLuigiDataTable occupies slot 18.
    MSL_CORE_EFFECT_BANK_CAPACITY = 19,
    MSL_CORE_EFFECT_QUEUE_CAPACITY = 256,
    MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY = 0x28,
    MSL_CORE_EFFECT_MODEL_START_CAPACITY = 4,
};

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
    // first header word is the bank's id base and only [id_base, count) exist
    // in the archive. Version-0 banks have id_base 0.
    s32 id_base;
} MslCoreEffectGeneratorBank;

typedef struct MslCoreEffectData {
    MslCoreEffectGeneratorBank banks[MSL_CORE_EFFECT_BANK_CAPACITY];
    MslCoreEffectModelStart
        common_model_start[MSL_CORE_EFFECT_COMMON_MODEL_CAPACITY];
    s8 recording_common_model;
} MslCoreEffectData;

typedef struct MslCoreEffectState {
    MslCoreEffectQueueNode nodes[MSL_CORE_EFFECT_QUEUE_CAPACITY];
    MslCoreEffectQueueNode* free;
} MslCoreEffectState;

void msl_effect_game_data_init(MslCoreEffectData* data);
void msl_effect_match_init(const MslCoreEffectData* data,
                           MslCoreEffectState* state);
void msl_effect_projection_bind(const MslCoreEffectData* data,
                                MslCoreEffectState* state);

#endif
