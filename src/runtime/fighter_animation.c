#include "runtime/fighter_animation.h"

#include "platform/memory.h"
#include "runtime/context.h"

#include <stddef.h>

#include <baselib/aobj.h>
#include <baselib/fobj.h>

int msl_fighter_anim_pool_init(MslFighterAnimPool* pool)
{
#ifdef MSL_CORE_NATIVE
    uint32_t i;

    pool->aobjs = HSD_MemAllocReloc(
        sizeof(*pool->aobjs) * MSL_FIGHTER_ANIM_AOBJ_CAPACITY,
        MSL_RELOC_HSD_AOBJ, MSL_FIGHTER_ANIM_AOBJ_CAPACITY,
        sizeof(*pool->aobjs), 0);
    pool->fobjs = HSD_MemAllocReloc(
        sizeof(*pool->fobjs) * MSL_FIGHTER_ANIM_FOBJ_CAPACITY,
        MSL_RELOC_HSD_FOBJ, MSL_FIGHTER_ANIM_FOBJ_CAPACITY,
        sizeof(*pool->fobjs), 0);
    pool->active_joints = HSD_MemAllocReloc(
        sizeof(*pool->active_joints) * MSL_FIGHTER_ANIM_AOBJ_CAPACITY,
        MSL_RELOC_POINTER_ARRAY, MSL_FIGHTER_ANIM_AOBJ_CAPACITY,
        sizeof(*pool->active_joints), 0);
    pool->free_aobjs = msl_memory_alloc(
        msl_core_memory_context(),
        sizeof(*pool->free_aobjs) * MSL_FIGHTER_ANIM_AOBJ_CAPACITY);
    pool->free_fobjs = msl_memory_alloc(
        msl_core_memory_context(),
        sizeof(*pool->free_fobjs) * MSL_FIGHTER_ANIM_FOBJ_CAPACITY);
    if (pool->aobjs == NULL || pool->fobjs == NULL ||
        pool->active_joints == NULL || pool->free_aobjs == NULL ||
        pool->free_fobjs == NULL)
    {
        return -1;
    }
    for (i = 0; i < MSL_FIGHTER_ANIM_AOBJ_CAPACITY; ++i) {
        pool->free_aobjs[i] =
            (uint16_t) (MSL_FIGHTER_ANIM_AOBJ_CAPACITY - i - 1);
    }
    for (i = 0; i < MSL_FIGHTER_ANIM_FOBJ_CAPACITY; ++i) {
        pool->free_fobjs[i] =
            (uint16_t) (MSL_FIGHTER_ANIM_FOBJ_CAPACITY - i - 1);
    }
    pool->free_aobj_count = MSL_FIGHTER_ANIM_AOBJ_CAPACITY;
    pool->free_fobj_count = MSL_FIGHTER_ANIM_FOBJ_CAPACITY;
    return 0;
#else
    (void) pool;
    return 0;
#endif
}
