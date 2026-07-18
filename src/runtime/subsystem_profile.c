#include "runtime/subsystem_profile.h"

#ifdef MSL_SUBSYSTEM_PROFILE

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { MSL_SUBSYSTEM_OWNER_CAPACITY = 4096 };

typedef struct MslSubsystemOwner {
    uintptr_t address;
    uint64_t cycles;
    uint64_t calls;
} MslSubsystemOwner;

static struct {
    uint64_t cycles[MSL_PROFILE_BUCKET_COUNT];
    uint64_t calls[MSL_PROFILE_BUCKET_COUNT];
    uint64_t timer_overhead;
    MslSubsystemOwner owners[MSL_SUBSYSTEM_OWNER_CAPACITY];
} profile;

static uint64_t net_cycles(uint64_t elapsed)
{
    return elapsed > profile.timer_overhead ? elapsed - profile.timer_overhead
                                            : 0;
}

void msl_profile_add(MslProfileBucket bucket, uint64_t elapsed)
{
    profile.cycles[bucket] += net_cycles(elapsed);
    profile.calls[bucket] += 1;
}

void msl_profile_add_owner(uintptr_t address, uint64_t elapsed)
{
    size_t slot = (address >> 4) & (MSL_SUBSYSTEM_OWNER_CAPACITY - 1);
    while (profile.owners[slot].address != 0 &&
           profile.owners[slot].address != address)
    {
        slot = (slot + 1) & (MSL_SUBSYSTEM_OWNER_CAPACITY - 1);
    }
    profile.owners[slot].address = address;
    profile.owners[slot].cycles += net_cycles(elapsed);
    profile.owners[slot].calls += 1;
}

void msl_subsystem_profile_reset(void)
{
    uint64_t best = UINT64_MAX;
    size_t i;
    memset(&profile, 0, sizeof(profile));
    for (i = 0; i < 4096; ++i) {
        uint64_t started = msl_profile_cycles();
        uint64_t elapsed = msl_profile_cycles() - started;
        if (elapsed < best) {
            best = elapsed;
        }
    }
    profile.timer_overhead = best;
}

void msl_subsystem_profile_report(void)
{
    static const char* const names[MSL_PROFILE_BUCKET_COUNT] = {
        "step",
        "observation",
        "terminal",
        "prepare",
        "scheduler",
        "finish",
        "fighter_animation",
        "action_anim_callback",
        "input_action_callback",
        "physics_callback",
        "stage_collision_callback",
        "contact_publication",
        "finish_fighter_visibility",
        "finish_item_matrices",
        "pose_animation",
        "action_script",
        "secondary_pose",
        "capture_pose",
    };
    size_t i;

    printf("subsystem_profile timer_overhead=%" PRIu64 "\n",
           profile.timer_overhead);
    for (i = 0; i < MSL_PROFILE_BUCKET_COUNT; ++i) {
        printf("subsystem_bucket name=%s calls=%" PRIu64 " cycles=%" PRIu64
               "\n",
               names[i], profile.calls[i], profile.cycles[i]);
    }
    for (i = 0; i < MSL_SUBSYSTEM_OWNER_CAPACITY; ++i) {
        if (profile.owners[i].address != 0) {
            printf("subsystem_owner address=%#" PRIxPTR " calls=%" PRIu64
                   " cycles=%" PRIu64 "\n",
                   profile.owners[i].address, profile.owners[i].calls,
                   profile.owners[i].cycles);
        }
    }
}

#endif
