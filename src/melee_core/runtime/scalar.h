#ifndef MSL_CORE_RUNTIME_SCALAR_H
#define MSL_CORE_RUNTIME_SCALAR_H

#include "runtime/camera.h"
#include "runtime/match.h"
#include "runtime/effects.h"
#include "runtime/wire.h"

#include "platform/slippi.h"

#include "ft/forward.h"
#include "gr/types.h"

#include <stddef.h>
#include <stdint.h>

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
    MslCoreEffectData effects;
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
    int32_t frame_id;
    uint32_t random_seed;
    MslCoreCompare output;
} MslCoreMatch;

int msl_core_game_data_init(MslCoreGameData* game_data, const char* data_root);
int msl_core_match_init(MslCoreMatch* match, const MslCoreGameData* game_data,
                        const MslCoreMatchConfig* config,
                        const MslCoreInput* previous_input);
int msl_core_match_step(MslCoreMatch* match, const MslCoreInput* input,
                        uint32_t frame_seed,
                        const MslCoreStageEvents* stage_events);
const MslCoreCompare* msl_core_match_output(const MslCoreMatch* match);

#endif
