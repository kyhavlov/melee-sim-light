#include "platform/memory.h"

#include <platform.h>

#include "runtime/context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MSL_CORE_NATIVE
#include "platform/native_dat.h"

#include <stdint.h>
#ifndef MSL_CORE_WASM
#include <sys/mman.h>
#endif
#endif

#include <dolphin/os/OSAlloc.h>

enum {
    MSL_MEMORY_GAME_DATA_BYTES = 64 * 1024 * 1024,
    MSL_MEMORY_MATCH_BYTES = 32 * 1024 * 1024,
};

int msl_memory_context_init(MslMemoryContext* context, MslMemoryOwner owner)
{
    size_t capacity = owner == MSL_MEMORY_GAME_DATA
                          ? MSL_MEMORY_GAME_DATA_BYTES
                          : MSL_MEMORY_MATCH_BYTES;

    if (context == NULL) {
        return -1;
    }
    memset(context, 0, sizeof(*context));
#ifdef MSL_CORE_NATIVE
#ifdef MSL_CORE_WASM
    context->arena = malloc(capacity);
    if (context->arena == NULL) {
        return -1;
    }
#else
    {
        void* mapping = mmap(NULL, capacity, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        if (mapping == MAP_FAILED || (uintptr_t) mapping > UINT32_MAX) {
            if (mapping != MAP_FAILED) {
                munmap(mapping, capacity);
            }
            return -1;
        }
        context->arena = mapping;
    }
#endif
#else
    context->arena = malloc(capacity);
    if (context->arena == NULL) {
        return -1;
    }
#endif
    context->capacity = capacity;
    return 0;
}

void msl_memory_context_reset(MslMemoryContext* context)
{
    if (context == NULL) {
        return;
    }
    context->used = 0;
    context->allocation_count = 0;
    context->sealed = 0;
}

void msl_memory_context_destroy(MslMemoryContext* context)
{
    if (context == NULL || context->arena == NULL) {
        return;
    }
#ifdef MSL_CORE_NATIVE
#ifdef MSL_CORE_WASM
    free(context->arena);
#else
    munmap(context->arena, context->capacity);
#endif
#else
    free(context->arena);
#endif
    memset(context, 0, sizeof(*context));
}

void* msl_memory_alloc(MslMemoryContext* context, size_t size)
{
    MslMemoryAllocation* allocation;
    size_t aligned_used;
    void* result;

    if (context == NULL || size == 0) {
        return NULL;
    }
    aligned_used = (context->used + MSL_MEMORY_ARENA_ALIGNMENT - 1) &
                   ~(size_t) (MSL_MEMORY_ARENA_ALIGNMENT - 1);
    if (context->sealed ||
        context->allocation_count == MSL_MEMORY_ALLOCATION_CAPACITY ||
        size > context->capacity - aligned_used)
    {
        abort();
    }
    result = context->arena + aligned_used;
    context->used = aligned_used + size;
    memset(result, 0, size);
    allocation = &context->allocations[context->allocation_count++];
    allocation->address = result;
    allocation->size = size;
    allocation->protected = 0;
    return result;
}

int msl_memory_context_owns(const MslMemoryContext* context,
                            const void* pointer)
{
    uintptr_t address = (uintptr_t) pointer;
    uintptr_t begin;
    if (context == NULL || context->arena == NULL || pointer == NULL) {
        return 0;
    }
    begin = (uintptr_t) context->arena;
    return address >= begin && address - begin < context->capacity;
}

void* HSD_MemAlloc(ssize_t size)
{
    void* result =
        size > 0 ? msl_memory_alloc(msl_core_memory_context(), (size_t) size)
                 : NULL;
#ifdef MSL_CORE_NATIVE
    msl_reloc_register(result, MSL_RELOC_RAW, 1,
                       size > 0 ? (uint32_t) size : 0, 0);
#endif
    return result;
}

#ifdef MSL_CORE_NATIVE
void* HSD_MemAllocReloc(size_t size, MslRelocType type, uint32_t count,
                        uint32_t stride, uint32_t flags)
{
    void* result = HSD_MemAlloc((ssize_t) size);
    msl_reloc_register(result, type, count, stride, flags);
    return result;
}
#endif

void msl_memory_finish_initialization(void)
{
    msl_core_memory_context()->sealed = 1;
}

void msl_memory_protect_allocation(const void* pointer)
{
    MslMemoryContext* context = msl_core_memory_context();
    MslMemoryContext* game_context = msl_core_game_memory_context();
    uintptr_t address = (uintptr_t) pointer;
    size_t i;
    int pass;

#ifdef MSL_CORE_NATIVE
    if (pointer == NULL || msl_native_dat_owns(pointer)) {
        abort();
    }
#endif
    for (pass = 0; pass < 2; ++pass) {
        MslMemoryContext* owner = pass == 0 ? context : game_context;
        if (pass != 0 && owner == context) {
            continue;
        }
        for (i = 0; i < owner->allocation_count; ++i) {
            MslMemoryAllocation* allocation = &owner->allocations[i];
            uintptr_t begin = (uintptr_t) allocation->address;
            if (allocation->address != NULL && address >= begin &&
                address - begin < allocation->size)
            {
                allocation->protected = 1;
                return;
            }
        }
    }
    abort();
}

void HSD_Free(void* ptr)
{
    MslMemoryContext* context;
    MslMemoryContext* game_context;

    if (ptr != NULL) {
        size_t i;
#ifdef MSL_CORE_NATIVE
        if (msl_native_dat_owns(ptr)) {
            return;
        }
#endif
        context = msl_core_memory_context();
        game_context = msl_core_game_memory_context();
        {
            int pass;
            for (pass = 0; pass < 2; ++pass) {
                MslMemoryContext* owner = pass == 0 ? context : game_context;
                if (pass != 0 && owner == context) {
                    continue;
                }
                for (i = 0; i < owner->allocation_count; ++i) {
                    MslMemoryAllocation* allocation = &owner->allocations[i];
                    if (allocation->address == ptr) {
                        if (allocation->protected) {
                            abort();
                        }
                        allocation->address = NULL;
                        return;
                    }
                }
            }
        }
        abort();
    }
}

long OSCheckHeap(int heap)
{
    (void) heap;
    return 0x40000000;
}

OSHeapHandle HSD_GetHeap(void)
{
    return 0;
}

void __assert(char* file, u32 line, char* condition)
{
    fprintf(stderr, "HSD assertion failed at %s:%lu: %s\n", file, line,
            condition);
    abort();
}
