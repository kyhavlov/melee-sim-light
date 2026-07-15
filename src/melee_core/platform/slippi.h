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
} MslCoreSlippiState;

void msl_slippi_state_init(MslCoreSlippiState* state);
void msl_slippi_state_bind(MslCoreSlippiState* state);
void msl_slippi_lcancel_set(struct Fighter* fp, u8 value);
u8 msl_slippi_lcancel_get(const struct Fighter* fp);

#endif
