#ifndef MSL_CORE_RUNTIME_CONTEXT_H
#define MSL_CORE_RUNTIME_CONTEXT_H

typedef struct MslCoreMatch MslCoreMatch;
typedef struct MslCoreGameData MslCoreGameData;
typedef struct MslMemoryContext MslMemoryContext;
typedef struct MslFileContext MslFileContext;
typedef struct MslSourceGameData MslSourceGameData;
typedef struct MslSourceMatchState MslSourceMatchState;
typedef struct HSD_ClassContext HSD_ClassContext;
typedef struct HSD_IDContext HSD_IDContext;
typedef struct MslFtDeviceState MslFtDeviceState;
typedef struct HSD_AObjContext HSD_AObjContext;
typedef struct MslFtCollState MslFtCollState;
typedef struct MslFtAnimScratch MslFtAnimScratch;
typedef struct MslFighterAnimPool MslFighterAnimPool;
typedef struct MslGroundState MslGroundState;
typedef struct mpCollisionBox mpCollisionBox;
typedef struct HSD_RandomContext HSD_RandomContext;
typedef struct HSD_ObjAllocContext HSD_ObjAllocContext;
typedef struct HSD_GObjContext HSD_GObjContext;
typedef struct StageInfo StageInfo;
typedef struct MslItemGameData MslItemGameData;
typedef struct MslItemState MslItemState;
typedef struct HSD_PadStatus HSD_PadStatus;
struct mpIsland_80458E88_t;
#ifdef MSL_CORE_NATIVE
typedef struct MslNativeDatContext MslNativeDatContext;
typedef struct Item Item;
typedef struct HSD_GObj HSD_GObj;
#endif

void msl_core_bind_game_data(MslCoreGameData* game_data);
void msl_core_bind_match(MslCoreMatch* match);

#ifdef MSL_CORE_NATIVE
// The hosted source port rebinds these once at each match entry. Imported
// gameplay otherwise reached tiny out-of-line accessors tens of millions of
// times per benchmark sample merely to recover retail-global owners. Publish
// the already-bound TLS pointers so hot translation units read the owner
// directly; the accessor symbols remain available to implementation and
// external code.
extern _Thread_local MslCoreMatch* msl_core_context_match;
extern _Thread_local const MslCoreGameData* msl_core_context_game_data;
extern _Thread_local HSD_RandomContext* msl_core_context_random;
extern _Thread_local HSD_ObjAllocContext* msl_core_context_objalloc;
extern _Thread_local HSD_GObjContext* msl_core_context_gobj;
extern _Thread_local HSD_ClassContext* msl_core_context_class;
extern _Thread_local HSD_IDContext* msl_core_context_id;
extern _Thread_local HSD_AObjContext* msl_core_context_aobj;
extern _Thread_local MslMemoryContext* msl_core_context_memory;
extern _Thread_local MslMemoryContext* msl_core_context_game_memory;
extern _Thread_local MslFileContext* msl_core_context_files;
extern _Thread_local MslSourceGameData* msl_core_context_source_game_data;
extern _Thread_local MslSourceMatchState* msl_core_context_source_match_state;
extern _Thread_local StageInfo* msl_core_context_stage_info;
extern _Thread_local int* msl_core_context_mp_collision_epoch;
extern _Thread_local mpCollisionBox* msl_core_context_mp_collision_boxes;
extern _Thread_local struct mpIsland_80458E88_t*
    msl_core_context_mp_island_root;
extern _Thread_local void** msl_core_context_fighter_common_data;
extern _Thread_local MslItemGameData* msl_core_context_item_game_data;
extern _Thread_local MslItemState* msl_core_context_item_state;
extern _Thread_local HSD_PadStatus* msl_core_context_pad_master;
extern _Thread_local HSD_PadStatus* msl_core_context_pad_game;
extern _Thread_local HSD_PadStatus* msl_core_context_pad_copy;
extern _Thread_local MslFtDeviceState* msl_core_context_ft_device;
extern _Thread_local MslFtCollState* msl_core_context_ft_coll;
extern _Thread_local MslFtAnimScratch* msl_core_context_ft_anim;
extern _Thread_local MslFighterAnimPool* msl_context_fighter_anim;
extern _Thread_local MslGroundState* msl_core_context_ground;
extern _Thread_local void* msl_core_context_ground_stage_positions;
extern _Thread_local void* msl_core_context_player_common_ref;
extern _Thread_local void* msl_core_context_fighter_data_list;
extern _Thread_local void* msl_core_context_fighter_state;
extern _Thread_local int* msl_core_context_fighter_reference_counts;
extern _Thread_local void* msl_core_context_fighter_costume_lists;
extern _Thread_local void* msl_core_context_fighter_animation_data;
extern _Thread_local void* msl_core_context_puff_hat_joints;
extern _Thread_local MslNativeDatContext* msl_core_context_native_dat;
#endif

MslCoreMatch* msl_core_active_match(void);
MslCoreMatch* msl_core_try_active_match(void);
MslMemoryContext* msl_core_memory_context(void);
MslMemoryContext* msl_core_game_memory_context(void);
MslFileContext* msl_core_file_context(void);
MslSourceGameData* msl_core_source_game_data(void);
MslSourceMatchState* msl_core_source_match_state(void);
HSD_ClassContext* msl_core_class_context(void);
HSD_IDContext* msl_core_id_context(void);
MslFtDeviceState* msl_core_ft_device_state(void);
HSD_AObjContext* msl_core_aobj_context(void);
MslFtCollState* msl_core_ft_coll_state(void);
MslFtAnimScratch* msl_core_ft_anim_scratch(void);
MslFighterAnimPool* msl_fighter_anim_pool(void);
MslGroundState* msl_core_ground_state(void);
void* msl_core_ground_stage_positions(void);
void** msl_core_stage_pointer_ref(int slot);
int* msl_core_mp_collision_epoch_ref(void);
mpCollisionBox* msl_core_mp_collision_boxes(void);
struct mpIsland_80458E88_t* msl_core_mp_island_root(void);
#ifdef MSL_CORE_NATIVE
MslNativeDatContext* msl_core_native_dat_context(void);
HSD_GObj* msl_core_peach_turnip_owner_get(const Item* item);
void msl_core_peach_turnip_owner_set(Item* item, HSD_GObj* owner);
#endif

#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_CONTEXT_IMPLEMENTATION)
#define msl_core_active_match() (msl_core_context_match)
#define msl_core_try_active_match() (msl_core_context_match)
#define msl_core_memory_context() (msl_core_context_memory)
#define msl_core_game_memory_context() (msl_core_context_game_memory)
#define msl_core_file_context() (msl_core_context_files)
#define msl_core_source_game_data() (msl_core_context_source_game_data)
#define msl_core_source_match_state() (msl_core_context_source_match_state)
#define msl_core_native_dat_context() (msl_core_context_native_dat)
#define msl_core_ft_coll_state() (msl_core_context_ft_coll)
#define msl_core_ft_anim_scratch() (msl_core_context_ft_anim)
#define msl_fighter_anim_pool() (msl_context_fighter_anim)
#define msl_core_ground_state() (msl_core_context_ground)
#define msl_core_ground_stage_positions()                                      \
    (msl_core_context_ground_stage_positions)
#endif

#endif
