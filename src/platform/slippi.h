#ifndef MSL_CORE_PLATFORM_SLIPPI_H
#define MSL_CORE_PLATFORM_SLIPPI_H

#include <platform.h>

struct Fighter;

typedef struct MslCoreSlippiFighterState {
    const struct Fighter* fighter;
    u8 lcancel;
} MslCoreSlippiFighterState;

typedef struct MslCoreSlippiState {
    MslCoreSlippiFighterState fighters[8];
    f32 fod_platform_height[2];
    u8 stage_event_streams;
    u8 fod_platform_known_mask;
    u8 fod_platform_pending_mask;
    u8 fod_platform_changed_mask;
    u8 dreamland_whispy_direction;
    u8 fighter_pre_random_seed_pending;
    u32 fighter_pre_random_seed;
} MslCoreSlippiState;

struct MslCoreStageEvents;

void msl_slippi_state_init(MslCoreSlippiState* state, u8 stage_event_streams);
void msl_slippi_state_bind(MslCoreSlippiState* state);
void msl_slippi_stage_events_begin(const struct MslCoreStageEvents* events);
bool msl_slippi_fod_platform_height(u8 platform, f32 source_height,
                                    f32* height, bool* changed);
bool msl_slippi_dreamland_whispy_direction(u8* direction);
void msl_slippi_apply_fighter_pre_random_seed(void);
void msl_slippi_lcancel_set(struct Fighter* fp, u8 value);
u8 msl_slippi_lcancel_get(const struct Fighter* fp);

#endif
