#include "runtime/context.h"

#include "runtime/scalar.h"

#include <stdlib.h>

typedef struct MslCoreContextBinding {
    MslCoreMatch* match;
    HSD_RandomContext* random;
    HSD_ObjAllocContext* objalloc;
    HSD_GObjContext* gobj;
    HSD_ClassContext* class_state;
    HSD_IDContext* id;
    HSD_AObjContext* aobj;
    MslMemoryContext* memory;
    MslMemoryContext* game_memory;
    MslFileContext* files;
    MslSourceGameData* source_game_data;
    MslSourceMatchState* source_match_state;
#ifdef MSL_CORE_NATIVE
    MslNativeDatContext* native_dat;
#endif
} MslCoreContextBinding;

static _Thread_local MslCoreContextBinding active;

void msl_core_bind_game_data(MslCoreGameData* game_data)
{
    active.match = NULL;
    active.random = &game_data->bootstrap_random;
    active.objalloc = &game_data->bootstrap_objalloc;
    active.gobj = &game_data->bootstrap_gobj;
    active.class_state = &game_data->bootstrap_class;
    active.id = &game_data->bootstrap_id;
    active.aobj = &game_data->bootstrap_aobj;
    active.memory = &game_data->memory;
    active.game_memory = &game_data->memory;
    active.files = &game_data->files;
    active.source_game_data = &game_data->source;
    active.source_match_state = NULL;
#ifdef MSL_CORE_NATIVE
    active.native_dat = &game_data->native_dat;
#endif
}

void msl_core_bind_match(MslCoreMatch* match)
{
    active.match = match;
    active.random = &match->random;
    active.objalloc = &match->objalloc;
    active.gobj = &match->gobj;
    active.class_state = &match->class_state;
    active.id = &match->id;
    active.aobj = &match->aobj;
    active.memory = &match->memory;
    active.game_memory = (MslMemoryContext*) &match->game_data->memory;
    active.files = (MslFileContext*) &match->game_data->files;
    active.source_game_data = (MslSourceGameData*) &match->game_data->source;
    active.source_match_state = &match->source;
#ifdef MSL_CORE_NATIVE
    active.native_dat = (MslNativeDatContext*) &match->game_data->native_dat;
#endif
}

MslCoreMatch* msl_core_active_match(void)
{
    if (active.match == NULL) {
        abort();
    }
    return active.match;
}

MslCoreMatch* msl_core_try_active_match(void)
{
    return active.match;
}

#ifdef MSL_CORE_NATIVE
HSD_GObj* msl_core_peach_turnip_owner_get(const Item* item)
{
    size_t i;
    MslCoreMatch* match = msl_core_active_match();
    for (i = 0; i < MSL_CORE_PEACH_TURNIP_OWNER_CAPACITY; ++i) {
        if (match->peach_turnip_owners[i].item == item) {
            return match->peach_turnip_owners[i].owner;
        }
    }
    return NULL;
}

void msl_core_peach_turnip_owner_set(Item* item, HSD_GObj* owner)
{
    size_t i;
    size_t empty = MSL_CORE_PEACH_TURNIP_OWNER_CAPACITY;
    MslCoreMatch* match = msl_core_active_match();
    for (i = 0; i < MSL_CORE_PEACH_TURNIP_OWNER_CAPACITY; ++i) {
        if (match->peach_turnip_owners[i].item == item) {
            if (owner == NULL) {
                match->peach_turnip_owners[i].item = NULL;
            }
            match->peach_turnip_owners[i].owner = owner;
            return;
        }
        if (empty == MSL_CORE_PEACH_TURNIP_OWNER_CAPACITY &&
            match->peach_turnip_owners[i].item == NULL)
        {
            empty = i;
        }
    }
    if (owner == NULL) {
        return;
    }
    if (empty == MSL_CORE_PEACH_TURNIP_OWNER_CAPACITY) {
        abort();
    }
    match->peach_turnip_owners[empty].item = item;
    match->peach_turnip_owners[empty].owner = owner;
}
#endif

HSD_RandomContext* msl_core_random_context(void)
{
    if (active.random == NULL) {
        abort();
    }
    return active.random;
}

HSD_ObjAllocContext* msl_core_objalloc_context(void)
{
    if (active.objalloc == NULL) {
        abort();
    }
    return active.objalloc;
}

HSD_GObjContext* msl_core_gobj_context(void)
{
    if (active.gobj == NULL) {
        abort();
    }
    return active.gobj;
}

HSD_ClassContext* msl_core_class_context(void)
{
    if (active.class_state == NULL) {
        abort();
    }
    return active.class_state;
}

HSD_IDContext* msl_core_id_context(void)
{
    if (active.id == NULL) {
        abort();
    }
    return active.id;
}

MslFtDeviceState* msl_core_ft_device_state(void)
{
    return &msl_core_source_match_state()->ft_device;
}

HSD_AObjContext* msl_core_aobj_context(void)
{
    if (active.aobj == NULL) {
        abort();
    }
    return active.aobj;
}

MslFtCollState* msl_core_ft_coll_state(void)
{
    return &msl_core_source_match_state()->ft_coll;
}

MslFtAnimScratch* msl_core_ft_anim_scratch(void)
{
    return &msl_core_source_match_state()->ft_anim;
}

MslGroundState* msl_core_ground_state(void)
{
    return &msl_core_source_match_state()->ground;
}

void* msl_core_ground_stage_positions(void)
{
    return msl_core_source_match_state()->ground.stage_positions;
}

void** msl_core_stage_pointer_ref(int slot)
{
    if ((unsigned) slot >= MSL_STAGE_POINTER_COUNT) {
        abort();
    }
    return &msl_core_source_match_state()->ground.stage_pointers[slot];
}

ftData** msl_core_fighter_data_list(void)
{
    MslCoreMatch* match = msl_core_try_active_match();
    return match != NULL ? match->source.fighter.data_list
                         : msl_core_source_game_data()->fighter.data_list;
}

struct UnkCostumeList* msl_core_fighter_costume_lists(void)
{
    return msl_core_source_game_data()->fighter.costume_lists;
}

UnkCostumeStruct* msl_core_fighter_costumes(FighterKind kind)
{
    MslSourceGameData* data = msl_core_source_game_data();
    switch (kind) {
    case FTKIND_FOX:
        return data->fighter.fox_costumes;
    case FTKIND_CAPTAIN:
        return data->fighter.falcon_costumes;
    case FTKIND_SEAK:
        return data->fighter.sheik_costumes;
    case FTKIND_PEACH:
        return data->fighter.peach_costumes;
    case FTKIND_PURIN:
        return data->fighter.puff_costumes;
    case FTKIND_MARS:
        return data->fighter.marth_costumes;
    case FTKIND_ZELDA:
        return data->fighter.zelda_costumes;
    case FTKIND_FALCO:
        return data->fighter.falco_costumes;
    default:
        return NULL;
    }
}

HSD_Joint** msl_core_puff_hat_joints(void)
{
    return msl_core_source_game_data()->fighter.puff_hat_joints;
}

ft_8045993C_t* msl_core_fighter_state(void)
{
    return msl_core_source_match_state()->fighter.state;
}

int* msl_core_fighter_reference_counts(void)
{
    return msl_core_source_match_state()->fighter.reference_counts;
}

void** msl_core_fighter_common_data(void)
{
    return msl_core_source_game_data()->fighter.common_data;
}

ftData_UnkCountStruct* msl_core_fighter_animation_data(void)
{
    return msl_core_source_game_data()->fighter.animation_data;
}

MslMemoryContext* msl_core_memory_context(void)
{
    if (active.memory == NULL) {
        abort();
    }
    return active.memory;
}

MslMemoryContext* msl_core_game_memory_context(void)
{
    if (active.game_memory == NULL) {
        abort();
    }
    return active.game_memory;
}

MslFileContext* msl_core_file_context(void)
{
    if (active.files == NULL) {
        abort();
    }
    return active.files;
}

MslSourceGameData* msl_core_source_game_data(void)
{
    if (active.source_game_data == NULL) {
        abort();
    }
    return active.source_game_data;
}

MslSourceMatchState* msl_core_source_match_state(void)
{
    if (active.source_match_state == NULL) {
        abort();
    }
    return active.source_match_state;
}

int* msl_core_mp_collision_epoch_ref(void)
{
    return &msl_core_source_match_state()->mp_coll.collision_epoch;
}

mpCollisionBox* msl_core_mp_collision_boxes(void)
{
    return msl_core_source_match_state()->mp_lib.collision_boxes;
}

struct mpIsland_80458E88_t* msl_core_mp_island_root(void)
{
    return &msl_core_source_match_state()->mp_lib.island_root;
}

pl_804D6470_t** msl_core_player_common_ref(void)
{
    return &msl_core_source_game_data()->player_common;
}

HSD_PadStatus* msl_core_pad_master_status(void)
{
    return msl_core_source_match_state()->pad_master;
}

HSD_PadStatus* msl_core_pad_game_status(void)
{
    return msl_core_source_match_state()->pad_game;
}

HSD_PadStatus* msl_core_pad_copy_status(void)
{
    return msl_core_source_match_state()->pad_copy;
}

StageInfo* msl_core_stage_info(void)
{
    return &msl_core_source_match_state()->stage;
}

MslItemGameData* msl_core_item_game_data(void)
{
    return &msl_core_source_game_data()->item;
}

MslItemState* msl_core_item_state(void)
{
    return &msl_core_source_match_state()->item;
}

CmSubject** msl_core_camera_last_subject_ref(void)
{
    return &msl_core_source_match_state()->camera.last_subject;
}

CameraDebugMode* msl_core_camera_debug(void)
{
    return &msl_core_source_match_state()->camera.debug;
}

#ifdef MSL_CORE_NATIVE
MslNativeDatContext* msl_core_native_dat_context(void)
{
    if (active.native_dat == NULL) {
        abort();
    }
    return active.native_dat;
}
#endif
