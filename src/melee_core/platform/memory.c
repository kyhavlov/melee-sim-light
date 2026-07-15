#include <platform.h>

#include <stdio.h>
#include <stdlib.h>
#ifdef MSL_CORE_NATIVE
#include "platform/native_dat.h"
#include <stdint.h>
#include <sys/mman.h>

static int msl_memory_initialization_complete;

typedef struct MslHsdAllocation {
    unsigned char* result;
    size_t requested_size;
    void* mapping;
    size_t mapping_size;
    int protected;
} MslHsdAllocation;

// Keep a fixed initialization registry with ample Fox/FD suite headroom so
// allocation ownership and DAT protection never require a host-side
// container grow.
enum { MSL_HSD_ALLOCATION_CAPACITY = 8192 };
static MslHsdAllocation msl_hsd_allocations[MSL_HSD_ALLOCATION_CAPACITY];
static size_t msl_hsd_allocation_count;
#endif

#include <dolphin/os/OSAlloc.h>

void* HSD_MemAlloc(ssize_t size)
{
    void* result;

    if (size <= 0) {
        return NULL;
    }
#ifdef MSL_CORE_NATIVE
    {
        const size_t page_size = 4096;
        size_t mapping_size =
            ((size_t) size + page_size - 1) & ~(page_size - 1);
        void* mapping;

        if (msl_memory_initialization_complete) {
            fprintf(stderr,
                    "HSD_MemAlloc reached after native match initialization "
                    "(%ld bytes)\n",
                    (long) size);
            abort();
        }
        // lbFile_800168A0 publishes archive buffers through a retail u32
        // out-parameter. Keep HSD-owned initialization allocations below 4 GiB
        // until that source ABI is promoted in the later per-match refactor.
        // refs/melee/src/melee/lb/lbarchive.c::lbFile_800168A0
        mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        if (mapping == MAP_FAILED) {
            fprintf(stderr, "HSD_MemAlloc failed for %ld bytes\n", (long) size);
            abort();
        }
        result = mapping;
        if ((uintptr_t) result > UINT32_MAX) {
            fprintf(stderr, "native HSD allocation is outside the 32-bit ABI\n");
            abort();
        }
        if (msl_hsd_allocation_count == MSL_HSD_ALLOCATION_CAPACITY) {
            fprintf(stderr, "native HSD allocation registry exhausted\n");
            abort();
        }
        msl_hsd_allocations[msl_hsd_allocation_count++] =
            (MslHsdAllocation) { result, (size_t) size, mapping, mapping_size,
                                 0 };
    }
#else
    result = malloc((size_t) size);
    if (result == NULL) {
        fprintf(stderr, "HSD_MemAlloc failed for %ld bytes\n", (long) size);
        abort();
    }
#endif
    return result;
}

#ifdef MSL_CORE_NATIVE
void msl_memory_finish_initialization(void)
{
    msl_memory_initialization_complete = 1;
}

void msl_memory_protect_allocation(const void* pointer)
{
    uintptr_t address = (uintptr_t) pointer;
    size_t i;

    if (pointer == NULL || msl_native_dat_owns(pointer)) {
        fprintf(stderr, "invalid raw DAT allocation protection request\n");
        abort();
    }
    for (i = 0; i < msl_hsd_allocation_count; ++i) {
        MslHsdAllocation* allocation = &msl_hsd_allocations[i];
        uintptr_t begin = (uintptr_t) allocation->result;
        if (allocation->result != NULL && address >= begin &&
            address - begin < allocation->requested_size)
        {
            if (!allocation->protected &&
                mprotect(allocation->mapping, allocation->mapping_size,
                         PROT_NONE) != 0)
            {
                perror("mprotect raw DAT allocation");
                abort();
            }
            allocation->protected = 1;
            return;
        }
    }
    fprintf(stderr, "raw DAT pointer %p has no HSD allocation owner\n",
            pointer);
    abort();
}
#endif

void HSD_Free(void* ptr)
{
#ifdef MSL_CORE_NATIVE
    if (ptr != NULL) {
        size_t i;
        if (msl_native_dat_owns(ptr)) {
            return;
        }
        for (i = 0; i < msl_hsd_allocation_count; ++i) {
            MslHsdAllocation* allocation = &msl_hsd_allocations[i];
            if (allocation->result == ptr) {
                if (allocation->protected) {
                    fprintf(stderr,
                            "attempted to free sealed raw DAT allocation\n");
                    abort();
                }
                munmap(allocation->mapping, allocation->mapping_size);
                allocation->result = NULL;
                return;
            }
        }
        fprintf(stderr, "HSD_Free received an unowned native pointer\n");
        abort();
    }
#else
    free(ptr);
#endif
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
