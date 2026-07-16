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
#include "it/forward.h"

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
#ifdef MSL_CORE_NATIVE
    MSL_CORE_PEACH_TURNIP_OWNER_CAPACITY = 16,
#endif
};

#ifdef MSL_CORE_NATIVE
typedef struct MslCorePeachTurnipOwner {
    Item* item;
    HSD_GObj* owner;
} MslCorePeachTurnipOwner;
#endif

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
#ifdef MSL_CORE_NATIVE
    // Retail stores Peach's live vegetable owner in a u32 source-layout
    // union. Native Match graphs can live above 4 GiB, so retain the complete
    // owner in relocatable match side state without changing Item offsets.
    // refs/melee/src/melee/it/{itCharItems.h,items/itpeachturnip.c}
    MslCorePeachTurnipOwner
        peach_turnip_owners[MSL_CORE_PEACH_TURNIP_OWNER_CAPACITY];
#endif
    // The PPC compatibility build links the same relocation substrate even
    // though only hosted construction activates it. Keep the compact owner
    // header layout available on every target; no PPC gameplay path reads it.
    uint32_t relocation_records_offset;
    uint32_t relocation_index_offset;
    uint32_t relocation_count;
    uint32_t relocation_record_capacity;
    uint32_t relocation_index_capacity;
    MslMemoryContext memory;
    int32_t frame_id;
    uint32_t last_frame_seed;
    float output_pos_x[MSL_CORE_MAX_PLAYERS];
    float output_pos_y[MSL_CORE_MAX_PLAYERS];
    uint8_t output_render_visibility[MSL_CORE_MAX_PLAYERS];
    uint32_t random_seed;
    MslCoreCompare output;
} MslCoreMatch;

int msl_core_game_data_init(MslCoreGameData* game_data, const char* data_root);
void msl_core_game_data_deinit(MslCoreGameData* game_data);
int msl_core_match_init(MslCoreMatch* match, const MslCoreGameData* game_data,
                        const MslCoreMatchConfig* config,
                        const MslCoreInput* previous_input);
int msl_core_match_storage_init(MslCoreMatch* match);
int msl_core_match_storage_bind(MslCoreMatch* match, uint8_t* arena,
                                size_t capacity);
int msl_core_match_reset(MslCoreMatch* match, const MslCoreGameData* game_data,
                         const MslCoreMatchConfig* config,
                         const MslCoreInput* previous_input);
void msl_core_match_destroy(MslCoreMatch* match);
int msl_core_match_step(MslCoreMatch* match, const MslCoreInput* input,
                        uint32_t frame_seed,
                        const MslCoreStageEvents* stage_events);
int msl_core_match_step_prepare(MslCoreMatch* match,
                                const MslCoreInput* input,
                                uint32_t frame_seed,
                                const MslCoreStageEvents* stage_events);
void msl_core_match_scheduler_begin(MslCoreMatch* match);
uint32_t msl_core_match_scheduler_priority_count(const MslCoreMatch* match);
void msl_core_match_scheduler_priority_begin(MslCoreMatch* match,
                                             uint32_t priority);
HSD_GObjEvent msl_core_match_scheduler_next_owner(MslCoreMatch* match);
void msl_core_match_scheduler_invoke(MslCoreMatch* match);
int msl_core_match_step_finish(MslCoreMatch* match, uint32_t frame_seed);
const MslCoreCompare* msl_core_match_output(const MslCoreMatch* match);
void msl_core_match_write_items(const MslCoreMatch* match,
                                MslCoreItem items[MSL_CORE_MAX_ITEMS]);

#endif
