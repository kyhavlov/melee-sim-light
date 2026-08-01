#ifndef _id_h_
#define _id_h_

#include <platform.h>

#include "baselib/objalloc.h"

typedef struct _IDEntry {
    struct _IDEntry* next;
    u32 id;
    void* data;
} IDEntry;

typedef struct _HSD_IDTable {
    struct _IDEntry* table[101];
} HSD_IDTable;

#ifdef MSL_CORE_HOSTED
// The default ID table indexes the live JObj graph, so it belongs to the
// Match that allocated those entries rather than process BSS.
// refs/melee/src/sysdolphin/baselib/id.c::{HSD_IDInsertToTable,
//   HSD_IDRemoveByIDFromTable,HSD_IDGetDataFromTable}
typedef struct HSD_IDContext {
    HSD_IDTable default_table;
} HSD_IDContext;

#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_CONTEXT_IMPLEMENTATION)
#include <runtime/context.h>
#define msl_core_id_context() (msl_core_context_id)
#else
HSD_IDContext* msl_core_id_context(void);
#endif
#endif

HSD_ObjAllocData* HSD_IDGetAllocData(void);
void HSD_IDInitAllocData(void);
void HSD_IDSetup(void);
void HSD_IDInsertToTable(HSD_IDTable* table, u32 id, void* data);
void HSD_IDRemoveByIDFromTable(HSD_IDTable* table, u32 id);
void* HSD_IDGetDataFromTable(HSD_IDTable* table, u32 id, s32* success);
void _HSD_IDForgetMemory(void* low, void* high);

#ifdef MSL_CORE_NATIVE
// Hosted HSD pointer identity is a deterministic tagged arena offset. The ID
// table remains source-width while true object/descriptor fields stay native
// pointers. Conversion aborts outside a bound owner; inverse lookup is
// intentionally limited to the native-DAT descriptors that consume it.
u32 msl_hsd_id_from_pointer(const void* pointer);
void* msl_hsd_native_dat_pointer_from_id(u32 id);
#else
#define msl_hsd_id_from_pointer(pointer) ((u32) (pointer))
#define msl_hsd_native_dat_pointer_from_id(id) ((void*) (id))
#endif

static inline void* HSD_IDGetData(u32 id, s32* success)
{
    return HSD_IDGetDataFromTable(NULL, id, success);
}

#endif
