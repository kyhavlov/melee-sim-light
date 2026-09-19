#define MSL_CORE_CONTEXT_IMPLEMENTATION
#include "runtime/context.h"

#include "runtime/scalar.h"

#include <stdlib.h>

_Thread_local MslCoreMatch* msl_core_context_match;
_Thread_local const MslCoreGameData* msl_core_context_game_data;
_Thread_local HSD_RandomContext* msl_core_context_random;
_Thread_local HSD_ObjAllocContext* msl_core_context_objalloc;
_Thread_local HSD_GObjContext* msl_core_context_gobj;
_Thread_local HSD_ClassContext* msl_core_context_class;
_Thread_local HSD_IDContext* msl_core_context_id;
_Thread_local HSD_AObjContext* msl_core_context_aobj;
_Thread_local MslMemoryContext* msl_core_context_memory;
_Thread_local MslMemoryContext* msl_core_context_game_memory;
_Thread_local MslFileContext* msl_core_context_files;
_Thread_local MslSourceGameData* msl_core_context_source_game_data;
_Thread_local MslSourceMatchState* msl_core_context_source_match_state;
_Thread_local StageInfo* msl_core_context_stage_info;
_Thread_local int* msl_core_context_mp_collision_epoch;
_Thread_local mpCollisionBox* msl_core_context_mp_collision_boxes;
_Thread_local struct mpIsland_80458E88_t* msl_core_context_mp_island_root;
_Thread_local void** msl_core_context_fighter_common_data;
_Thread_local MslItemGameData* msl_core_context_item_game_data;
_Thread_local MslItemState* msl_core_context_item_state;
_Thread_local HSD_PadStatus* msl_core_context_pad_master;
_Thread_local HSD_PadStatus* msl_core_context_pad_game;
_Thread_local HSD_PadStatus* msl_core_context_pad_copy;
_Thread_local MslFtDeviceState* msl_core_context_ft_device;
_Thread_local MslFtCollState* msl_core_context_ft_coll;
_Thread_local MslFtAnimScratch* msl_core_context_ft_anim;
_Thread_local MslFighterPose* msl_context_fighter_pose;
_Thread_local const MslFighterPosePrograms* msl_context_fighter_pose_programs;
_Thread_local MslGroundState* msl_core_context_ground;
_Thread_local void* msl_core_context_ground_stage_positions;
_Thread_local void* msl_core_context_player_common_ref;
_Thread_local void* msl_core_context_fighter_data_list;
_Thread_local void* msl_core_context_fighter_state;
_Thread_local int* msl_core_context_fighter_reference_counts;
_Thread_local void* msl_core_context_fighter_costume_lists;
_Thread_local void* msl_core_context_fighter_animation_data;
_Thread_local void* msl_core_context_puff_hat_joints;
#ifdef MSL_CORE_NATIVE
_Thread_local MslNativeDatContext* msl_core_context_native_dat;
#endif

void msl_core_bind_game_data(MslCoreGameData* game_data)
{
    msl_core_context_match = NULL;
    msl_core_context_game_data = game_data;
    msl_core_context_random = &game_data->bootstrap_random;
    msl_core_context_objalloc = &game_data->bootstrap_objalloc;
    msl_core_context_gobj = &game_data->bootstrap_gobj;
    msl_core_context_class = &game_data->bootstrap_class;
    msl_core_context_id = &game_data->bootstrap_id;
    msl_core_context_aobj = &game_data->bootstrap_aobj;
    msl_core_context_memory = &game_data->memory;
    msl_core_context_game_memory = &game_data->memory;
    msl_core_context_files = &game_data->files;
    msl_core_context_source_game_data = &game_data->source;
    msl_core_context_source_match_state = NULL;
    msl_core_context_stage_info = NULL;
    msl_core_context_mp_collision_epoch = NULL;
    msl_core_context_mp_collision_boxes = NULL;
    msl_core_context_mp_island_root = NULL;
    msl_core_context_fighter_common_data = game_data->source.fighter.common_data;
    msl_core_context_item_game_data = &game_data->source.item;
    msl_core_context_item_state = NULL;
    msl_core_context_pad_master = NULL;
    msl_core_context_pad_game = NULL;
    msl_core_context_pad_copy = NULL;
    msl_core_context_ft_device = NULL;
    msl_core_context_ft_coll = NULL;
    msl_core_context_ft_anim = NULL;
    msl_context_fighter_pose = NULL;
    msl_context_fighter_pose_programs = &game_data->fighter_pose_programs;
    msl_core_context_ground = NULL;
    msl_core_context_ground_stage_positions = NULL;
    msl_core_context_player_common_ref = &game_data->source.player_common;
    msl_core_context_fighter_data_list =
        game_data->source.fighter.data_list;
    msl_core_context_fighter_state = NULL;
    msl_core_context_fighter_reference_counts = NULL;
    msl_core_context_fighter_costume_lists =
        game_data->source.fighter.costume_lists;
    msl_core_context_fighter_animation_data =
        game_data->source.fighter.animation_data;
    msl_core_context_puff_hat_joints =
        game_data->source.fighter.puff_hat_joints;
#ifdef MSL_CORE_NATIVE
    msl_core_context_native_dat = &game_data->native_dat;
#endif
}

void msl_core_bind_match(MslCoreMatch* match)
{
    msl_core_context_match = match;
    msl_core_context_game_data = match->game_data;
    msl_core_context_random = &match->random;
    msl_core_context_objalloc = &match->objalloc;
    msl_core_context_gobj = &match->gobj;
    msl_core_context_class = &match->class_state;
    msl_core_context_id = &match->id;
    msl_core_context_aobj = &match->aobj;
    msl_core_context_memory = &match->memory;
    msl_core_context_game_memory =
        (MslMemoryContext*) &match->game_data->memory;
    msl_core_context_files = (MslFileContext*) &match->game_data->files;
    msl_core_context_source_game_data =
        (MslSourceGameData*) &match->game_data->source;
    msl_core_context_source_match_state = &match->source;
    msl_core_context_stage_info = &match->source.stage;
    msl_core_context_mp_collision_epoch =
        &match->source.mp_coll.collision_epoch;
    msl_core_context_mp_collision_boxes =
        match->source.mp_lib.collision_boxes;
    msl_core_context_mp_island_root = &match->source.mp_lib.island_root;
    msl_core_context_fighter_common_data =
        (void**) match->game_data->source.fighter.common_data;
    msl_core_context_item_game_data =
        (MslItemGameData*) &match->game_data->source.item;
    msl_core_context_item_state = &match->source.item;
    msl_core_context_pad_master = match->source.pad_master;
    msl_core_context_pad_game = match->source.pad_game;
    msl_core_context_pad_copy = match->source.pad_copy;
    msl_core_context_ft_device = &match->source.ft_device;
    msl_core_context_ft_coll = &match->source.ft_coll;
    msl_core_context_ft_anim = &match->source.ft_anim;
    msl_context_fighter_pose = &match->fighter_pose;
    msl_context_fighter_pose_programs =
        &match->game_data->fighter_pose_programs;
    msl_core_context_ground = &match->source.ground;
    msl_core_context_ground_stage_positions =
        match->source.ground.stage_positions;
    msl_core_context_player_common_ref =
        (void*) &match->game_data->source.player_common;
    msl_core_context_fighter_data_list = match->source.fighter.data_list;
    msl_core_context_fighter_state = match->source.fighter.state;
    msl_core_context_fighter_reference_counts =
        match->source.fighter.reference_counts;
    msl_core_context_fighter_costume_lists =
        (void*) match->game_data->source.fighter.costume_lists;
    msl_core_context_fighter_animation_data =
        (void*) match->game_data->source.fighter.animation_data;
    msl_core_context_puff_hat_joints =
        (void*) match->game_data->source.fighter.puff_hat_joints;
#ifdef MSL_CORE_NATIVE
    msl_core_context_native_dat =
        (MslNativeDatContext*) &match->game_data->native_dat;
#endif
}

MslCoreMatch* msl_core_active_match(void)
{
    if (msl_core_context_match == NULL) {
        abort();
    }
    return msl_core_context_match;
}

MslCoreMatch* msl_core_try_active_match(void)
{
    return msl_core_context_match;
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
    if (msl_core_context_random == NULL) {
        abort();
    }
    return msl_core_context_random;
}

HSD_ObjAllocContext* msl_core_objalloc_context(void)
{
    if (msl_core_context_objalloc == NULL) {
        abort();
    }
    return msl_core_context_objalloc;
}

HSD_GObjContext* msl_core_gobj_context(void)
{
    if (msl_core_context_gobj == NULL) {
        abort();
    }
    return msl_core_context_gobj;
}

HSD_ClassContext* msl_core_class_context(void)
{
    if (msl_core_context_class == NULL) {
        abort();
    }
    return msl_core_context_class;
}

HSD_IDContext* msl_core_id_context(void)
{
    if (msl_core_context_id == NULL) {
        abort();
    }
    return msl_core_context_id;
}

MslFtDeviceState* msl_core_ft_device_state(void)
{
    return &msl_core_source_match_state()->ft_device;
}

HSD_AObjContext* msl_core_aobj_context(void)
{
    if (msl_core_context_aobj == NULL) {
        abort();
    }
    return msl_core_context_aobj;
}

MslFtCollState* msl_core_ft_coll_state(void)
{
    return &msl_core_source_match_state()->ft_coll;
}

MslFtAnimScratch* msl_core_ft_anim_scratch(void)
{
    return &msl_core_source_match_state()->ft_anim;
}

MslFighterPose* msl_fighter_pose(void)
{
    return &msl_core_active_match()->fighter_pose;
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
    case FTKIND_SAMUS:
        return data->fighter.samus_costumes;
    case FTKIND_MEWTWO:
        return data->fighter.mewtwo_costumes;
    case FTKIND_GAMEWATCH:
        return data->fighter.gamewatch_costumes;
    case FTKIND_NESS:
        return data->fighter.ness_costumes;
    case FTKIND_LINK:
        return data->fighter.link_costumes;
    case FTKIND_CLINK:
        return data->fighter.clink_costumes;
    case FTKIND_POPO:
        return data->fighter.popo_costumes;
    case FTKIND_NANA:
        return data->fighter.nana_costumes;
    case FTKIND_DONKEY:
        return data->fighter.donkey_costumes;
    case FTKIND_GANON:
        return data->fighter.ganon_costumes;
    case FTKIND_YOSHI:
        return data->fighter.yoshi_costumes;
    case FTKIND_KOOPA:
        return data->fighter.koopa_costumes;
    case FTKIND_PIKACHU:
        return data->fighter.pikachu_costumes;
    case FTKIND_LUIGI:
        return data->fighter.luigi_costumes;
    case FTKIND_MARIO:
        return data->fighter.mario_costumes;
    case FTKIND_DRMARIO:
        return data->fighter.drmario_costumes;
    case FTKIND_MARS:
        return data->fighter.marth_costumes;
    case FTKIND_EMBLEM:
        return data->fighter.roy_costumes;
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
    if (msl_core_context_memory == NULL) {
        abort();
    }
    return msl_core_context_memory;
}

MslMemoryContext* msl_core_game_memory_context(void)
{
    if (msl_core_context_game_memory == NULL) {
        abort();
    }
    return msl_core_context_game_memory;
}

MslFileContext* msl_core_file_context(void)
{
    if (msl_core_context_files == NULL) {
        abort();
    }
    return msl_core_context_files;
}

MslSourceGameData* msl_core_source_game_data(void)
{
    if (msl_core_context_source_game_data == NULL) {
        abort();
    }
    return msl_core_context_source_game_data;
}

MslSourceMatchState* msl_core_source_match_state(void)
{
    if (msl_core_context_source_match_state == NULL) {
        abort();
    }
    return msl_core_context_source_match_state;
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
    if (msl_core_context_native_dat == NULL) {
        abort();
    }
    return msl_core_context_native_dat;
}
#endif
