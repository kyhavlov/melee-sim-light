#include "platform/slippi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MslCoreSlippiState* msl_bound_slippi_state;

// refs/slippi-ssbm-asm/Recording/GetLCancelStatus/GetLCancelStatus.asm
void msl_slippi_state_bind(MslCoreSlippiState* state)
{
    msl_bound_slippi_state = state;
}

void msl_slippi_state_init(MslCoreSlippiState* state)
{
    memset(state, 0, sizeof(*state));
    msl_slippi_state_bind(state);
}

static MslCoreSlippiFighterState* find_state(const struct Fighter* fp,
                                             bool create)
{
    int i;
    MslCoreSlippiFighterState* fighter_state =
        msl_bound_slippi_state->fighters;
    MslCoreSlippiFighterState* free_state = NULL;

    for (i = 0;
         i < (int) (sizeof(msl_bound_slippi_state->fighters) /
                    sizeof(msl_bound_slippi_state->fighters[0]));
         ++i)
    {
        if (fighter_state[i].fighter == fp) {
            return &fighter_state[i];
        }
        if (free_state == NULL && fighter_state[i].fighter == NULL) {
            free_state = &fighter_state[i];
        }
    }
    if (!create) {
        return NULL;
    }
    if (free_state == NULL) {
        fprintf(stderr, "hosted Slippi fighter-state capacity exceeded\n");
        abort();
    }
    free_state->fighter = fp;
    return free_state;
}

void msl_slippi_lcancel_set(struct Fighter* fp, u8 value)
{
    find_state(fp, true)->lcancel = value;
}

u8 msl_slippi_lcancel_get(const struct Fighter* fp)
{
    MslCoreSlippiFighterState* state = find_state(fp, false);
    return state == NULL ? 0 : state->lcancel;
}
