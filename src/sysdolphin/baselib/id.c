#include "id.h"

#include "debug.h"

#include <__mem.h>
#ifdef MSL_CORE_HOSTED
#include <runtime/context.h>
#endif
#ifdef MSL_CORE_NATIVE
#include <platform/memory.h>
#include <platform/native_dat.h>

#ifndef MSL_CORE_WASM
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#endif

HSD_ObjAllocData hsd_iddata;

#ifdef MSL_CORE_HOSTED
#define default_table (msl_core_id_context()->default_table)
#else
HSD_IDTable default_table;
#endif

HSD_ObjAllocData* HSD_IDGetAllocData(void)
{
    return &hsd_iddata;
}

#ifdef MSL_CORE_NATIVE
enum {
    MSL_HSD_ID_OFFSET_MASK = 0x3FFFFFFF,
    MSL_HSD_ID_REGION_MASK = 0xC0000000,
    MSL_HSD_ID_NATIVE_DAT = 0x40000000,
    MSL_HSD_ID_MATCH = 0x80000000,
    MSL_HSD_ID_IMAGE = 0xC0000000,
};

static const uint8_t hsd_id_image_anchor;

u32 msl_hsd_id_from_pointer(const void* pointer)
{
#ifndef MSL_CORE_WASM
    Dl_info image_info;
    Dl_info owner_info;
#endif
    uintptr_t begin;
    uintptr_t offset;
    uintptr_t value = (uintptr_t) pointer;
    if (pointer == NULL) {
        return 0;
    }
    if (msl_core_context_native_dat != NULL &&
        msl_core_context_native_dat->arena != NULL)
    {
        begin = (uintptr_t) msl_core_context_native_dat->arena;
        if (value >= begin &&
            value - begin < msl_core_context_native_dat->arena_used)
        {
            if (value - begin > MSL_HSD_ID_OFFSET_MASK) {
                fprintf(stderr, "native DAT HSD id exceeds its tagged range\n");
                abort();
            }
            return MSL_HSD_ID_NATIVE_DAT | (u32) (value - begin);
        }
    }
    if (msl_core_context_match != NULL &&
        msl_core_context_memory != NULL &&
        msl_core_context_memory->arena != NULL)
    {
        begin = (uintptr_t) msl_core_context_memory->arena;
        if (value >= begin && value - begin < msl_core_context_memory->used) {
            if (value - begin > MSL_HSD_ID_OFFSET_MASK) {
                fprintf(stderr, "Match HSD id exceeds its tagged range\n");
                abort();
            }
            return MSL_HSD_ID_MATCH | (u32) (value - begin);
        }
    }
#ifdef MSL_CORE_WASM
    // Wasm pointers are deterministic linear-memory guest addresses. Static
    // descriptors therefore already have a stable image-relative currency.
    if ((uintptr_t) pointer <= MSL_HSD_ID_OFFSET_MASK) {
        return MSL_HSD_ID_IMAGE | (u32) (uintptr_t) pointer;
    }
#else
    if (dladdr(&hsd_id_image_anchor, &owner_info) != 0 &&
        dladdr(pointer, &image_info) != 0 && owner_info.dli_fbase != NULL &&
        image_info.dli_fbase == owner_info.dli_fbase)
    {
        offset = value - (uintptr_t) image_info.dli_fbase;
        if (offset > MSL_HSD_ID_OFFSET_MASK) {
            fprintf(stderr, "HSD image id exceeds its tagged range\n");
            abort();
        }
        return MSL_HSD_ID_IMAGE | (u32) offset;
    }
#endif
    fprintf(stderr, "HSD pointer id has no bound arena owner\n");
    abort();
}

void* msl_hsd_native_dat_pointer_from_id(u32 id)
{
    u32 offset = id & MSL_HSD_ID_OFFSET_MASK;
    if (id == 0) {
        return NULL;
    }
    if ((id & MSL_HSD_ID_REGION_MASK) != MSL_HSD_ID_NATIVE_DAT ||
        msl_core_context_native_dat == NULL ||
        msl_core_context_native_dat->arena == NULL ||
        offset >= msl_core_context_native_dat->arena_used)
    {
        fprintf(stderr, "invalid native DAT HSD pointer id %08x\n", id);
        abort();
    }
    return msl_core_context_native_dat->arena + offset;
}
#endif

void HSD_IDInitAllocData(void)
{
    HSD_ObjAllocInit(&hsd_iddata, sizeof(IDEntry), 4);
}

void HSD_IDSetup(void)
{
    memset(&default_table, 0, sizeof(HSD_IDTable));
}

inline u32 hash(u32 id)
{
    return id % 0x65;
}

inline IDEntry* IDEntryAlloc(void)
{
    IDEntry* entry;

    entry = HSD_ObjAlloc(&hsd_iddata);
    /// @todo Convert to @c HSD_ASSERT once a byte-matching form is found.
    if (entry == NULL) {
        __assert("id.c", 67, "entry");
    }
    memset(entry, 0, sizeof(IDEntry));

    return entry;
}

void HSD_IDInsertToTable(HSD_IDTable* table, u32 id, void* data)
{
    IDEntry* entry;

    if (table == NULL) {
        table = &default_table;
    }

    entry = table->table[hash(id)];
    while (entry != NULL) {
        if (entry->id == id) {
            break;
        }
        entry = entry->next;
    }

    if (entry != NULL) {
        entry->id = id;
        entry->data = data;
    } else {
        entry = IDEntryAlloc();
        entry->id = id;
        entry->data = data;
        entry->next = table->table[hash(id)];
        table->table[hash(id)] = entry;
    }
}

inline void IDEntryFree(IDEntry* entry)
{
    HSD_ObjFree(&hsd_iddata, entry);
}

void HSD_IDRemoveByIDFromTable(HSD_IDTable* table, u32 id)
{
    IDEntry* entry;
    IDEntry* prev;

    if (table == NULL) {
        table = &default_table;
    }

    prev = NULL;
    for (entry = table->table[hash(id)]; entry != NULL; entry = entry->next) {
        if (entry->id == id) {
            if (prev != NULL) {
                prev->next = entry->next;
            } else {
                table->table[hash(id)] = entry->next;
            }
            IDEntryFree(entry);
            return;
        }
        prev = entry;
    }
}

void* HSD_IDGetDataFromTable(HSD_IDTable* table, u32 id, s32* success)
{
    IDEntry* entry;

    if (table == NULL) {
        table = &default_table;
    }

    entry = table->table[hash(id)];
    while (entry != NULL) {
        if (entry->id == id) {
            if (success != NULL) {
                *success = 1;
            }
            return entry->data;
        }
        entry = entry->next;
    }

    if (success != NULL) {
        *success = 0;
    }
    return NULL;
}

void _HSD_IDForgetMemory(void* low, void* high)
{
    memset(&default_table, 0, sizeof(HSD_IDTable));
}
