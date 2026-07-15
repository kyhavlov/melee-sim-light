#include <platform.h>

#include <stdio.h>
#include <stdlib.h>

#include <dolphin/os/OSAlloc.h>

void* HSD_MemAlloc(ssize_t size)
{
    void* result;

    if (size <= 0) {
        return NULL;
    }
    result = malloc((size_t) size);
    if (result == NULL) {
        fprintf(stderr, "HSD_MemAlloc failed for %ld bytes\n", (long) size);
        abort();
    }
    return result;
}

void HSD_Free(void* ptr)
{
    free(ptr);
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
