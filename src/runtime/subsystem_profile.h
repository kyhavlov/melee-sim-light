#ifndef MSL_RUNTIME_SUBSYSTEM_PROFILE_H
#define MSL_RUNTIME_SUBSYSTEM_PROFILE_H

#ifdef MSL_SUBSYSTEM_PROFILE

#include <stdint.h>
#include <x86intrin.h>

typedef enum MslProfileBucket {
    MSL_PROFILE_STEP,
    MSL_PROFILE_OBSERVATION,
    MSL_PROFILE_TERMINAL,
    MSL_PROFILE_PREPARE,
    MSL_PROFILE_SCHEDULER,
    MSL_PROFILE_FINISH,
    MSL_PROFILE_FIGHTER_ANIMATION,
    MSL_PROFILE_ACTION_ANIM_CALLBACK,
    MSL_PROFILE_INPUT_ACTION_CALLBACK,
    MSL_PROFILE_PHYSICS_CALLBACK,
    MSL_PROFILE_STAGE_COLLISION_CALLBACK,
    MSL_PROFILE_CONTACT_PUBLICATION,
    MSL_PROFILE_FINISH_FIGHTER_VISIBILITY,
    MSL_PROFILE_FINISH_ITEM_MATRICES,
    MSL_PROFILE_POSE_ANIMATION,
    MSL_PROFILE_ACTION_SCRIPT,
    MSL_PROFILE_SECONDARY_POSE,
    MSL_PROFILE_CAPTURE_POSE,
    MSL_PROFILE_BUCKET_COUNT,
} MslProfileBucket;

static inline uint64_t msl_profile_cycles(void)
{
    unsigned int aux;
    return __rdtscp(&aux);
}

void msl_subsystem_profile_reset(void);
void msl_subsystem_profile_report(void);
void msl_profile_add(MslProfileBucket bucket, uint64_t elapsed);
void msl_profile_add_owner(uintptr_t owner, uint64_t elapsed);

#endif

#endif
