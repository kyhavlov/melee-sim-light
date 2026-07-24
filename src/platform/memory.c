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
// Linux uses one generic value on every architecture; glibc only exposes it
// under _GNU_SOURCE.
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#endif
#endif

#include <dolphin/os/OSAlloc.h>

enum {
    MSL_MEMORY_GAME_DATA_BYTES = 64 * 1024 * 1024,
    // Native supported-domain construction remains below 2.75 MiB after
    // bounded source pools and exact relocation metadata. Keep a measured
    // 3 MiB hard ceiling; runtime allocation is forbidden after sealing.
    // The PPC compatibility oracle still constructs source-width DAT graphs
    // inside its Match arena and is not a resident production-batch target.
    // tests/melee_core/runtime_census.c
#ifdef MSL_CORE_NATIVE
    MSL_MEMORY_MATCH_BYTES = 3 * 1024 * 1024,
#else
    MSL_MEMORY_MATCH_BYTES = 8 * 1024 * 1024,
#endif
};

// Allocation provenance is needed while a Match graph is constructed, but
// the sealed bump arena never frees or grows during gameplay. One thread-local
// construction ledger is therefore reused across sequential Match resets;
// immutable GameData retains its own initialization ledger.
static _Thread_local MslMemoryAllocation
    match_allocations[MSL_MEMORY_ALLOCATION_CAPACITY];

static int bind_allocation_ledger(MslMemoryContext* context,
                                  MslMemoryOwner owner)
{
    context->owner = (uint8_t) owner;
    context->allocation_capacity = MSL_MEMORY_ALLOCATION_CAPACITY;
    if (owner == MSL_MEMORY_MATCH) {
        context->allocations = match_allocations;
        return 0;
    }
    context->allocations = calloc(MSL_MEMORY_ALLOCATION_CAPACITY,
                                  sizeof(*context->allocations));
    return context->allocations != NULL ? 0 : -1;
}

int msl_memory_context_init(MslMemoryContext* context, MslMemoryOwner owner)
{
    size_t capacity = owner == MSL_MEMORY_GAME_DATA
                          ? MSL_MEMORY_GAME_DATA_BYTES
                          : MSL_MEMORY_MATCH_BYTES;

    if (context == NULL) {
        return -1;
    }
    memset(context, 0, sizeof(*context));
    if (bind_allocation_ledger(context, owner) != 0) {
        return -1;
    }
#ifdef MSL_CORE_NATIVE
#ifdef MSL_CORE_WASM
    context->arena = malloc(capacity);
    if (context->arena == NULL) {
        if (owner == MSL_MEMORY_GAME_DATA) {
            free(context->allocations);
        }
        context->allocations = NULL;
        return -1;
    }
#else
    {
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
        void* mapping;
        // Raw retail archives still pass through 32-bit source APIs while
        // GameData is built. Match graphs use native pointers throughout and
        // must not consume the process-wide low-address window.
        // refs/melee/src/melee/lb/lbfile.c::{lbFile_80016580,
        //   lbFile_800168A0}
        if (owner == MSL_MEMORY_GAME_DATA) {
            flags |= MAP_32BIT;
        }
        mapping = mmap(NULL, capacity, PROT_READ | PROT_WRITE, flags, -1, 0);
        if (owner == MSL_MEMORY_GAME_DATA &&
            (mapping == MAP_FAILED || (uintptr_t) mapping > UINT32_MAX))
        {
            // MAP_32BIT is x86-64-only; non-x86 kernels (e.g. Docker on an
            // arm64 host running amd64 user space) ignore or reject it. Scan
            // fixed low hints above the native DAT arena window instead, the
            // same low-address strategy native_alloc uses for DAT graphs.
            uintptr_t hint;
            if (mapping != MAP_FAILED) {
                munmap(mapping, capacity);
                mapping = MAP_FAILED;
            }
            for (hint = 0x60000000u;
                 hint + capacity <= UINT32_MAX && mapping == MAP_FAILED;
                 hint += 0x04000000u)
            {
                mapping = mmap((void*) hint, capacity,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS |
                                   MAP_FIXED_NOREPLACE,
                               -1, 0);
            }
        }
        if (mapping == MAP_FAILED ||
            (owner == MSL_MEMORY_GAME_DATA &&
             (uintptr_t) mapping > UINT32_MAX))
        {
            if (mapping != MAP_FAILED) {
                munmap(mapping, capacity);
            }
            if (owner == MSL_MEMORY_GAME_DATA) {
                free(context->allocations);
            }
            context->allocations = NULL;
            return -1;
        }
        context->arena = mapping;
    }
#endif
#else
    context->arena = malloc(capacity);
    if (context->arena == NULL) {
        if (owner == MSL_MEMORY_GAME_DATA) {
            free(context->allocations);
        }
        context->allocations = NULL;
        return -1;
    }
#endif
    context->capacity = capacity;
    context->arena_owned = 1;
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
    if (context->owner == MSL_MEMORY_MATCH) {
        context->allocations = match_allocations;
        context->allocation_capacity = MSL_MEMORY_ALLOCATION_CAPACITY;
    }
}

void msl_memory_context_reuse_match(MslMemoryContext* context,
                                    uint8_t* arena, size_t capacity,
                                    uint8_t arena_owned)
{
    memset(context, 0, sizeof(*context));
    context->arena = arena;
    context->capacity = capacity;
    context->owner = MSL_MEMORY_MATCH;
    context->arena_owned = arena_owned;
    context->allocations = match_allocations;
    context->allocation_capacity = MSL_MEMORY_ALLOCATION_CAPACITY;
}

void msl_memory_context_bind_match(MslMemoryContext* context, uint8_t* arena,
                                   size_t capacity)
{
    msl_memory_context_reuse_match(context, arena, capacity, 0);
}

void msl_memory_context_destroy(MslMemoryContext* context)
{
    MslMemoryAllocation* allocations;
    uint8_t owner;
    if (context == NULL) {
        return;
    }
    allocations = context->allocations;
    owner = context->owner;
    if (context->arena != NULL && context->arena_owned) {
#ifdef MSL_CORE_NATIVE
#ifdef MSL_CORE_WASM
        free(context->arena);
#else
        munmap(context->arena, context->capacity);
#endif
#else
        free(context->arena);
#endif
    }
    if (owner == MSL_MEMORY_GAME_DATA) {
        free(allocations);
    }
    memset(context, 0, sizeof(*context));
}

size_t msl_memory_match_capacity(void)
{
    return MSL_MEMORY_MATCH_BYTES;
}

void* msl_memory_map_match_arenas(size_t count)
{
    size_t bytes;
    if (count == 0 || count > SIZE_MAX / MSL_MEMORY_MATCH_BYTES) {
        return NULL;
    }
    bytes = count * MSL_MEMORY_MATCH_BYTES;
#ifdef MSL_CORE_NATIVE
#ifdef MSL_CORE_WASM
    return malloc(bytes);
#else
    {
        void* mapping = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return mapping == MAP_FAILED ? NULL : mapping;
    }
#endif
#else
    return malloc(bytes);
#endif
}

void msl_memory_unmap_match_arenas(void* mapping, size_t count)
{
    if (mapping == NULL || count == 0) {
        return;
    }
#ifdef MSL_CORE_NATIVE
#ifdef MSL_CORE_WASM
    free(mapping);
#else
    munmap(mapping, count * MSL_MEMORY_MATCH_BYTES);
#endif
#else
    free(mapping);
#endif
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
    if (context->sealed || context->allocations == NULL ||
        context->allocation_count == context->allocation_capacity ||
        size > context->capacity - aligned_used)
    {
        fprintf(stderr,
                "Melee core arena allocation failed: owner=%u sealed=%u "
                "size=%zu used=%zu capacity=%zu allocations=%zu/%zu\n",
                context->owner, context->sealed, size, context->used,
                context->capacity, context->allocation_count,
                context->allocation_capacity);
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
    MslMemoryContext* context = msl_core_memory_context();
    context->sealed = 1;
    if (context->owner == MSL_MEMORY_MATCH) {
        context->allocations = NULL;
        context->allocation_capacity = 0;
    }
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
        for (i = 0; owner->allocations != NULL &&
                    i < owner->allocation_count; ++i)
        {
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
        if (context->owner == MSL_MEMORY_MATCH &&
            msl_memory_context_owns(context, ptr))
        {
            return;
        }
        {
            int pass;
            for (pass = 0; pass < 2; ++pass) {
                MslMemoryContext* owner = pass == 0 ? context : game_context;
                if (pass != 0 && owner == context) {
                    continue;
                }
                for (i = 0; owner->allocations != NULL &&
                            i < owner->allocation_count; ++i)
                {
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
