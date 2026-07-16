#include "runtime/relocation.h"

#include "runtime/context.h"
#include "runtime/scalar.h"

#include <stdlib.h>
#include <string.h>
#include <baselib/class.h>

static uint32_t address_hash(const void* address)
{
    uintptr_t value = (uintptr_t) address >> 4;
    value ^= value >> 17;
    value *= UINT32_C(0xED5AD4BB);
    value ^= value >> 11;
    return (uint32_t) value & (MSL_RELOC_INDEX_CAPACITY - 1);
}

static uint32_t* find_index_slot(MslRelocRegistry* registry,
                                 const void* address)
{
    uint32_t slot = address_hash(address);
    for (;;) {
        uint32_t entry = registry->index[slot];
        if (entry == 0 || registry->objects[entry - 1].address == address) {
            return &registry->index[slot];
        }
        slot = (slot + 1) & (MSL_RELOC_INDEX_CAPACITY - 1);
    }
}

void msl_reloc_rebuild_index(MslRelocRegistry* registry)
{
    uint32_t i;
    memset(registry->index, 0, sizeof(registry->index));
    for (i = 0; i < registry->count; ++i) {
        uint32_t* slot =
            find_index_slot(registry, registry->objects[i].address);
        if (*slot != 0) {
            abort();
        }
        *slot = i + 1;
    }
}

void msl_reloc_register(void* address, MslRelocType type, uint32_t count,
                        uint32_t stride, uint32_t flags)
{
    MslCoreMatch* match = msl_core_try_active_match();
    MslRelocRegistry* registry;
    uint32_t* slot;

    if (match == NULL || address == NULL || count == 0) {
        return;
    }
    registry = &match->relocation;
    slot = find_index_slot(registry, address);
    if (*slot != 0) {
        MslRelocObject* object = &registry->objects[*slot - 1];
        object->type = type;
        object->count = count;
        object->stride = stride;
        object->flags = flags;
        return;
    }
    if (registry->count == MSL_RELOC_OBJECT_CAPACITY) {
        abort();
    }
    registry->objects[registry->count].address = address;
    registry->objects[registry->count].type = type;
    registry->objects[registry->count].count = count;
    registry->objects[registry->count].stride = stride;
    registry->objects[registry->count].flags = flags;
    *slot = registry->count + 1;
    ++registry->count;
}

void msl_reloc_register_hsd_class(void* address, HSD_ClassInfo* info)
{
    HSD_ClassInfo* cursor;
    MslRelocType type = MSL_RELOC_RAW;

    for (cursor = info; cursor != NULL; cursor = cursor->head.parent) {
        const char* name = cursor->head.class_name;
        if (name == NULL) {
            continue;
        }
        if (strcmp(name, "hsd_jobj") == 0) {
            type = MSL_RELOC_HSD_JOBJ;
        } else if (strcmp(name, "hsd_dobj") == 0) {
            type = MSL_RELOC_HSD_DOBJ;
        } else if (strcmp(name, "hsd_mobj") == 0) {
            type = MSL_RELOC_HSD_MOBJ;
        } else if (strcmp(name, "hsd_tobj") == 0) {
            type = MSL_RELOC_HSD_TOBJ;
        } else if (strcmp(name, "hsd_cobj") == 0) {
            type = MSL_RELOC_HSD_COBJ;
        } else if (strcmp(name, "hsd_wobj") == 0 ||
                   strcmp(name, "had_wobj") == 0)
        {
            type = MSL_RELOC_HSD_WOBJ;
        } else if (strcmp(name, "hsd_pobj") == 0) {
            type = MSL_RELOC_HSD_POBJ;
        } else if (strcmp(name, "hsd_lobj") == 0) {
            type = MSL_RELOC_HSD_LOBJ;
        } else {
            continue;
        }
        break;
    }
    msl_reloc_register(address, type, 1,
                       type != MSL_RELOC_RAW ? msl_reloc_type_descs[type].size
                                             : (uint32_t) info->head.obj_size,
                       MSL_RELOC_INTRUSIVE_FIRST_POINTER);
}
