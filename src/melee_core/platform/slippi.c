#include "platform/slippi.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct MslSlippiFighterState {
    const struct Fighter* fighter;
    u8 lcancel;
} MslSlippiFighterState;

static MslSlippiFighterState fighter_state[8];

static MslSlippiFighterState* find_state(const struct Fighter* fp, bool create)
{
    int i;
    MslSlippiFighterState* free_state = NULL;

    for (i = 0; i < (int) (sizeof(fighter_state) / sizeof(fighter_state[0]));
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
    MslSlippiFighterState* state = find_state(fp, false);
    return state == NULL ? 0 : state->lcancel;
}
