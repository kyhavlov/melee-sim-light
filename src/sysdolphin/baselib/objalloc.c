#include "objalloc.h"

#include "initialize.h"
#include "memory.h"

#include <__mem.h>
#include <dolphin/os/OSAlloc.h>
#ifdef MSL_CORE_NATIVE
#include <runtime/relocation.h>

#endif

#ifdef MSL_CORE_HOSTED
#define obj_heap (msl_core_objalloc_context()->heap)
#define alloc_datas (msl_core_objalloc_context()->alloc_datas)
#else
static objheap obj_heap = { 0, 0, -1, -1 };
static HSD_ObjAllocData* alloc_datas;
#endif

#ifdef MSL_CORE_HOSTED
HSD_ObjAllocData* HSD_ObjAllocResolve(HSD_ObjAllocData* data)
{
    HSD_ObjAllocContext* context = msl_core_objalloc_context();
    u32 i;

    if (data == NULL) {
        return NULL;
    }
    if (data >= context->values &&
        data < context->values + HSD_OBJALLOC_CONTEXT_CAPACITY)
    {
        return data;
    }
    for (i = 0; i < context->count; ++i) {
        if (context->keys[i] == data) {
            return &context->values[i];
        }
    }
    if (context->count == HSD_OBJALLOC_CONTEXT_CAPACITY) {
        abort();
    }
    context->keys[context->count] = data;
    return &context->values[context->count++];
}

void HSD_ObjAllocSetRelocType(HSD_ObjAllocData* data, u32 type, u32 count,
                              u32 stride)
{
    HSD_ObjAllocContext* context = msl_core_objalloc_context();
    HSD_ObjAllocData* resolved = HSD_ObjAllocResolve(data);
    u32 index = (u32) (resolved - context->values);
    context->reloc_types[index] = type;
    context->reloc_counts[index] = count;
    context->reloc_strides[index] = stride;
}
#endif

void HSD_ObjAllocPreallocateAll(u32 minimum_free, size_t maximum_free_bytes)
{
    HSD_ObjAllocData* data;

    // Hosted scalar simulation seals HSD allocation after bootstrap. Grow
    // every source-registered object pool up front; normal allocation still
    // consumes and returns the original intrusive free lists.
    // refs/melee/src/sysdolphin/baselib/objalloc.c::HSD_ObjAlloc
    for (data = alloc_datas; data != NULL; data = data->next) {
        u32 bounded_free = minimum_free;
        if (data->size != 0 && maximum_free_bytes / data->size < bounded_free) {
            bounded_free = maximum_free_bytes / data->size;
        }
        if (bounded_free == 0) {
            bounded_free = 1;
        }
        if (data->free < bounded_free) {
            HSD_ObjAllocAddFree(data, bounded_free - data->free);
        }
    }
}

void HSD_ObjAllocEnsureFree(HSD_ObjAllocData* data, u32 minimum_free)
{
    data = HSD_ObjAllocResolve(data);
    if (data != NULL && data->free < minimum_free) {
        HSD_ObjAllocAddFree(data, minimum_free - data->free);
    }
}

void HSD_ObjSetHeap(u32 size, void* ptr)
{
    obj_heap.curr = (uintptr_t) ptr;
    obj_heap.top = (uintptr_t) ptr;
    obj_heap.remain = size;
    obj_heap.size = size;
}

s32 HSD_ObjAllocAddFree(HSD_ObjAllocData* data, u32 num)
{
    uintptr_t computed_start;
    uintptr_t pool_end;
    u32 pool_size;
    u8* pool_start;

    u8 _[4];

    data = HSD_ObjAllocResolve(data);
    HSD_ASSERT(0xEE, data);
    pool_size = data->size * num;
    if (obj_heap.top != 0) {
        pool_end = obj_heap.top + obj_heap.size;
        computed_start = (obj_heap.curr + data->align) & ~data->align;
        pool_start = (void*) computed_start;
        if (computed_start > pool_end) {
            return 0;
        }
        if (pool_end - (uintptr_t) pool_start < pool_size) {
            pool_size = pool_end - (uintptr_t) pool_start -
                        (pool_end - (uintptr_t) pool_start) % data->size;
        }
        num = pool_size / data->size;
        if (num == 0) {
            return 0;
        }
        obj_heap.curr = (uintptr_t) pool_start + pool_size;
        obj_heap.remain = pool_end - obj_heap.curr;
    } else {
        pool_start = HSD_MemAlloc(pool_size);
        if (pool_start == 0) {
            return 0;
        }
        obj_heap.remain -= pool_size;
    }

    {
        int i;
        for (i = 0; (unsigned) i < num - 1; i++) {
            *(void**) (pool_start + data->size * i) =
                (void*) (pool_start + data->size * (i + 1));
        }
        *(void**) (pool_start + data->size * i) = data->freehead;
    }

    data->freehead = (HSD_ObjAllocLink*) pool_start;
    data->free += num;
#ifdef MSL_CORE_NATIVE
    {
        u32 i;
        for (i = 0; i < num; ++i) {
            // A newly added slot is an intrusive free-list node, not a live
            // instance of the allocator's object type yet.
            msl_reloc_register(pool_start + data->size * i, MSL_RELOC_RAW, 1,
                               data->size, MSL_RELOC_INTRUSIVE_FIRST_POINTER);
        }
    }
#endif
    return num;
}

void* HSD_ObjAlloc(HSD_ObjAllocData* data)
{
    HSD_ObjAllocLink* cur;
    u32 size;

    data = HSD_ObjAllocResolve(data);
    if (data->num_limit_flag && data->used >= data->num_limit) {
        return NULL;
    }
    if (data->heap_limit_flag) {
        if (data->heap_limit_num == (unsigned) -1) {
            if (obj_heap.top != 0) {
                size = obj_heap.remain;
            } else {
                size = OSCheckHeap(HSD_GetHeap());
            }
            if (size <= data->heap_limit_size) {
                data->heap_limit_num = data->used + data->free;
            }
        } else {
            if (obj_heap.top != 0) {
                size = obj_heap.remain;
            } else {
                size = OSCheckHeap(HSD_GetHeap());
            }
            if (size > data->heap_limit_size) {
                data->heap_limit_num = -1;
            }
        }
        if (data->used >= data->heap_limit_num) {
            return NULL;
        }
    }
    if (data->free == 0) {
        HSD_ObjAllocAddFree(data, 1);
        if (data->free == 0) {
            return NULL;
        }
    }
    cur = data->freehead;
    data->freehead = cur->next;
    data->used += 1;
    data->free -= 1;
    if (data->used > data->peak) {
        data->peak = data->used;
    }
#ifdef MSL_CORE_NATIVE
    {
        HSD_ObjAllocContext* context = msl_core_objalloc_context();
        u32 index = (u32) (data - context->values);
        msl_reloc_register(
            cur, context->reloc_types[index],
            context->reloc_counts[index] != 0 ? context->reloc_counts[index]
                                              : 1,
            context->reloc_strides[index] != 0 ? context->reloc_strides[index]
                                               : data->size,
            0);
    }
#endif
    return cur;
}

void HSD_ObjFree(HSD_ObjAllocData* data, void* obj)
{
    data = HSD_ObjAllocResolve(data);
    HSD_ObjAllocLink* link = obj;
    link->next = data->freehead;
    data->freehead = link;
    data->free += 1;
    data->used -= 1;
#ifdef MSL_CORE_NATIVE
    msl_reloc_register(obj, MSL_RELOC_RAW, 1, data->size,
                       MSL_RELOC_INTRUSIVE_FIRST_POINTER);
#endif
}

inline void removeAll(HSD_ObjAllocData* data)
{
    data = HSD_ObjAllocResolve(data);
    HSD_ObjAllocData** cur = &alloc_datas;
    while (*cur != NULL) {
        if (*cur == data) {
            *cur = (*cur)->next;
        } else {
            cur = &(*cur)->next;
        }
    }
}

void HSD_ObjAllocInit(HSD_ObjAllocData* data, size_t size, u32 align)
{
    data = HSD_ObjAllocResolve(data);
    HSD_ASSERT(0x185, data);
    if (data != NULL) {
        removeAll(data);
    } else {
        alloc_datas = NULL;
    }
    memset(data, 0, sizeof(HSD_ObjAllocData));
    data->num_limit = -1;
    data->heap_limit_size = 0;
    data->heap_limit_num = -1;
    data->align = align - 1;
    data->size = (size + data->align) & ~data->align;
    data->next = alloc_datas;
    alloc_datas = data;
}

void _HSD_ObjAllocForgetMemory(void* low, void* high)
{
    alloc_datas = NULL;
}
