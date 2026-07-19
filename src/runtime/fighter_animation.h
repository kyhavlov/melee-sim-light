#ifndef MSL_RUNTIME_FIGHTER_ANIMATION_H
#define MSL_RUNTIME_FIGHTER_ANIMATION_H

#include <stdint.h>

#include <baselib/forward.h>

enum {
    // Fixed fighter ownership plus the generic 128/256 reserves in scalar.c
    // preserves the previous source-backed 704-AObj/1280-FObj Match bound.
    MSL_FIGHTER_ANIM_AOBJ_CAPACITY = 576,
    MSL_FIGHTER_ANIM_FOBJ_CAPACITY = 1024,
};

typedef struct MslFighterAnimPool {
    HSD_AObj* aobjs;
    HSD_FObj* fobjs;
    HSD_JObj** active_joints;
    uint16_t* free_aobjs;
    uint16_t* free_fobjs;
    uint16_t active_count;
    uint16_t free_aobj_count;
    uint16_t free_fobj_count;
} MslFighterAnimPool;

int msl_fighter_anim_pool_init(MslFighterAnimPool* pool);

#endif
