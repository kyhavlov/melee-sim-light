#include "platform/slippi.h"

#include "runtime/wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <baselib/random.h>
#include <ft/types.h>

static _Thread_local MslCoreSlippiState* msl_bound_slippi_state;

// refs/slippi-ssbm-asm/Recording/GetLCancelStatus/GetLCancelStatus.asm
void msl_slippi_state_bind(MslCoreSlippiState* state)
{
    msl_bound_slippi_state = state;
}

void msl_slippi_state_init(MslCoreSlippiState* state, u8 stage_event_streams)
{
    memset(state, 0, sizeof(*state));
    state->stage_event_streams = stage_event_streams;
    msl_slippi_state_bind(state);
}

void msl_slippi_stage_events_begin(const MslCoreStageEvents* events)
{
    msl_bound_slippi_state->fod_platform_pending_mask =
        events->fod_platform_mask;
    msl_bound_slippi_state->fod_platform_changed_mask =
        events->fod_platform_mask;
    if ((events->fod_platform_mask & 1) != 0) {
        msl_bound_slippi_state->fod_platform_height[0] =
            events->fod_platform_height[0];
    }
    if ((events->fod_platform_mask & 2) != 0) {
        msl_bound_slippi_state->fod_platform_height[1] =
            events->fod_platform_height[1];
    }
    if (events->dreamland_whispy_valid != 0) {
        msl_bound_slippi_state->dreamland_whispy_direction =
            events->dreamland_whispy_direction;
    }
    msl_bound_slippi_state->fighter_pre_random_seed_pending =
        events->fighter_pre_random_seed_valid;
    msl_bound_slippi_state->fighter_pre_random_seed =
        events->fighter_pre_random_seed;
    memcpy(msl_bound_slippi_state->cpu_inputs, events->cpu_inputs,
           sizeof(events->cpu_inputs));
}

bool msl_slippi_fod_platform_height(u8 platform, f32 source_height,
                                    f32* height, bool* changed)
{
    u8 bit;
    if ((msl_bound_slippi_state->stage_event_streams & 1) == 0 ||
        platform >= 2)
    {
        return false;
    }
    bit = (u8) (1U << platform);
    *changed = (msl_bound_slippi_state->fod_platform_changed_mask & bit) != 0;
    if ((msl_bound_slippi_state->fod_platform_pending_mask & bit) != 0) {
        msl_bound_slippi_state->fod_platform_known_mask |= bit;
        msl_bound_slippi_state->fod_platform_pending_mask &= (u8) ~bit;
    } else if ((msl_bound_slippi_state->fod_platform_known_mask & bit) == 0) {
        // The event only records changes, so the stage DAT's initial height
        // owns the value until the first event for this platform.
        msl_bound_slippi_state->fod_platform_height[platform] = source_height;
        msl_bound_slippi_state->fod_platform_known_mask |= bit;
    }
    *height = msl_bound_slippi_state->fod_platform_height[platform];
    return true;
}



bool msl_slippi_dreamland_whispy_direction(u8* direction)
{
    if ((msl_bound_slippi_state->stage_event_streams & 2) == 0) {
        return false;
    }
    *direction = msl_bound_slippi_state->dreamland_whispy_direction;
    return true;
}

void msl_slippi_apply_fighter_pre_random_seed(void)
{
    if (msl_bound_slippi_state->fighter_pre_random_seed_pending != 0) {
        // Recording/SendFrameStart.s runs at the beginning of the source
        // scheduler. SendGamePreFrame.asm observes the same stream later at
        // Fighter_Spaghetti_8006AD10+0x3D0 (GALE01 0x8006B0E0), after any
        // earlier stage/effect callbacks. Replay validation supplies both
        // source observations; ordinary free-running steps leave this lane
        // invalid and advance HSD RNG without intervention.
        // refs/slippi-ssbm-asm/Recording/{SendFrameStart.s,SendGamePreFrame.asm}
        *seed_ptr = msl_bound_slippi_state->fighter_pre_random_seed;
        msl_bound_slippi_state->fighter_pre_random_seed_pending = 0;
    }
}

void msl_slippi_apply_cpu_input(struct Fighter* fp)
{
    MslReplayCpuInput* input;
    if (fp->x221F_b4 || fp->player_id >= MSL_CORE_MAX_PLAYERS) {
        return;
    }
    input = &msl_bound_slippi_state->cpu_inputs[fp->player_id];
    if (!input->valid) {
        return;
    }
    // Playback/Core/RestoreGameFrame.asm, before input edges and timers.
    fp->input.lstick.x = input->main_x;
    fp->input.lstick.y = input->main_y;
    fp->input.cstick.x = input->c_x;
    fp->input.cstick.y = input->c_y;
    fp->input.x650 = input->trigger;
    fp->input.held_inputs = input->buttons;
    // Validation also supplies SendGamePreFrame's RNG observation here.
    // Slippi playback itself only restores RNG when resync is enabled.
    *seed_ptr = input->random_seed;
    input->valid = 0;
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
