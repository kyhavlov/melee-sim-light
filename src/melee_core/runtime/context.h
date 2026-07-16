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
typedef struct MslGroundState MslGroundState;
typedef struct mpCollisionBox mpCollisionBox;
struct mpIsland_80458E88_t;
#ifdef MSL_CORE_NATIVE
typedef struct MslNativeDatContext MslNativeDatContext;
typedef struct Item Item;
typedef struct HSD_GObj HSD_GObj;
#endif

void msl_core_bind_game_data(MslCoreGameData* game_data);
void msl_core_bind_match(MslCoreMatch* match);
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

#endif
