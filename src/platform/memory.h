#ifndef MSL_CORE_PLATFORM_MEMORY_H
#define MSL_CORE_PLATFORM_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#ifdef MSL_CORE_NATIVE
#include "runtime/relocation.h"
#endif

enum {
    MSL_MEMORY_ALLOCATION_CAPACITY = 8192,
    MSL_MEMORY_ARENA_ALIGNMENT = 32,
};

typedef enum MslMemoryOwner {
    MSL_MEMORY_GAME_DATA,
    MSL_MEMORY_MATCH,
} MslMemoryOwner;

typedef struct MslMemoryAllocation {
    uint8_t* address;
    size_t size;
    uint8_t protected;
} MslMemoryAllocation;

typedef struct MslMemoryContext {
    uint8_t* arena;
    MslMemoryAllocation* allocations;
    size_t capacity;
    size_t used;
    size_t allocation_count;
    size_t allocation_capacity;
    uint8_t owner;
    uint8_t sealed;
    uint8_t arena_owned;
} MslMemoryContext;

int msl_memory_context_init(MslMemoryContext* context, MslMemoryOwner owner);
void msl_memory_context_reset(MslMemoryContext* context);
void msl_memory_context_reuse_match(MslMemoryContext* context,
                                    uint8_t* arena, size_t capacity,
                                    uint8_t arena_owned);
void msl_memory_context_bind_match(MslMemoryContext* context, uint8_t* arena,
                                   size_t capacity);
void msl_memory_context_destroy(MslMemoryContext* context);
size_t msl_memory_match_capacity(void);
void* msl_memory_map_match_arenas(size_t count);
void msl_memory_unmap_match_arenas(void* mapping, size_t count);
void* msl_memory_alloc(MslMemoryContext* context, size_t size);
#ifdef MSL_CORE_NATIVE
void* HSD_MemAllocReloc(size_t size, MslRelocType type, uint32_t count,
                        uint32_t stride, uint32_t flags);
#endif
int msl_memory_context_owns(const MslMemoryContext* context,
                            const void* pointer);

// Seal hosted HSD allocation after the match bootstrap, and make a raw
// game-file allocation inaccessible once its native DAT graph is complete.
void msl_memory_finish_initialization(void);
void msl_memory_protect_allocation(const void* pointer);

#endif
