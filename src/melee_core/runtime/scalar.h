#ifndef MSL_CORE_RUNTIME_SCALAR_H
#define MSL_CORE_RUNTIME_SCALAR_H

#include "platform/files.h"
#include "platform/memory.h"
#include "platform/slippi.h"
#include "runtime/camera.h"
#include "runtime/effects.h"
#include "runtime/match.h"
#include "runtime/relocation.h"
#include "runtime/source_state.h"
#include "runtime/wire.h"
#ifdef MSL_CORE_NATIVE
#include "platform/native_dat.h"
#endif
#include "ft/forward.h"

#include "gr/types.h"

#include <stddef.h>
#include <stdint.h>
#include <baselib/aobj.h>
#include <baselib/class.h>
#include <baselib/gobj.h>
#include <baselib/id.h>
#include <baselib/objalloc.h>
#include <baselib/random.h>

enum {
    MSL_CORE_DATA_ROOT_CAPACITY = 1024,
    MSL_CORE_STAGE_GROUND_CAPACITY = 10,
};

// Private scalar ownership boundary. GameData is initialized once and passed
// read-only to matches; mutable headless/runtime ownership belongs to the
// match. Source callbacks that lack a context parameter are rebound at each
// init/step entry rather than owning parallel runtime state themselves.
typedef struct MslCoreGameData {
    char root[MSL_CORE_DATA_ROOT_CAPACITY];
    uint64_t fingerprint;
    MslFileContext files;
    MslCoreEffectData effects;
    MslSourceGameData source;
    HSD_RandomContext bootstrap_random;
    HSD_ObjAllocContext bootstrap_objalloc;
    HSD_GObjContext bootstrap_gobj;
    HSD_ClassContext bootstrap_class;
    HSD_IDContext bootstrap_id;
    HSD_AObjContext bootstrap_aobj;
    MslMemoryContext memory;
#ifdef MSL_CORE_NATIVE
    MslNativeDatContext native_dat;
#endif
} MslCoreGameData;

typedef struct MslCoreMatch {
    const MslCoreGameData* game_data;
    Fighter_GObj* fighters[MSL_CORE_MAX_PLAYERS];
    Ground stage_ground[MSL_CORE_STAGE_GROUND_CAPACITY];
    uint8_t stage_ground_used[MSL_CORE_STAGE_GROUND_CAPACITY];
    StageData* stage_data;
    uint8_t source_slots[MSL_CORE_MAX_PLAYERS];
    MslCoreMatchConfig config;
    MslCoreMatchRules rules;
    MslCoreCameraState camera;
    MslCoreEffectState effects;
    MslCoreSlippiState slippi;
    MslSourceMatchState source;
    HSD_RandomContext random;
    HSD_ObjAllocContext objalloc;
    HSD_GObjContext gobj;
    HSD_ClassContext class_state;
    HSD_IDContext id;
    HSD_AObjContext aobj;
    MslMemoryContext memory;
    MslRelocRegistry relocation;
    int32_t frame_id;
    uint32_t random_seed;
    MslCoreCompare output;
} MslCoreMatch;

int msl_core_game_data_init(MslCoreGameData* game_data, const char* data_root);
void msl_core_game_data_deinit(MslCoreGameData* game_data);
int msl_core_match_init(MslCoreMatch* match, const MslCoreGameData* game_data,
                        const MslCoreMatchConfig* config,
                        const MslCoreInput* previous_input);
int msl_core_match_storage_init(MslCoreMatch* match);
int msl_core_match_reset(MslCoreMatch* match, const MslCoreGameData* game_data,
                         const MslCoreMatchConfig* config,
                         const MslCoreInput* previous_input);
void msl_core_match_destroy(MslCoreMatch* match);
int msl_core_match_step(MslCoreMatch* match, const MslCoreInput* input,
                        uint32_t frame_seed,
                        const MslCoreStageEvents* stage_events);
const MslCoreCompare* msl_core_match_output(const MslCoreMatch* match);

#endif
