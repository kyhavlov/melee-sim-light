#include "runtime/relocation.h"

#include "runtime/context.h"
#include "runtime/scalar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <baselib/class.h>

static _Thread_local MslRelocRegistry build_registry;
static _Thread_local MslCoreMatch* build_match;

enum {
    // The class size allocator can split an already-registered free piece at
    // runtime, creating a new typed subobject address without growing the
    // sealed arena. Bound that metadata separately from construction records.
    // refs/melee/src/sysdolphin/baselib/class.c::hsdAllocMemPiece
    MSL_RELOC_RUNTIME_RECORD_RESERVE = 512,
};

static uint32_t build_address_hash(const void* address)
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
    uint32_t slot = build_address_hash(address);
    for (;;) {
        uint32_t entry = registry->index[slot];
        if (entry == 0 || registry->objects[entry - 1].address == address) {
            return &registry->index[slot];
        }
        slot = (slot + 1) & (MSL_RELOC_INDEX_CAPACITY - 1);
    }
}

static uint32_t compact_address_hash(uint32_t address, uint32_t mask)
{
    uint32_t value = address;
    value ^= value >> 16;
    value *= UINT32_C(0x7FEB352D);
    value ^= value >> 15;
    return value & mask;
}

static uint32_t encode_address(const MslCoreMatch* match, const void* address)
{
    uintptr_t value = (uintptr_t) address;
    uintptr_t match_begin = (uintptr_t) match;
    uintptr_t arena_begin = (uintptr_t) match->memory.arena;
    uintptr_t offset;

    if (value >= match_begin && value - match_begin < sizeof(*match)) {
        offset = value - match_begin;
        if (offset > MSL_RELOC_ADDRESS_OFFSET_MASK) {
            abort();
        }
        return MSL_RELOC_ADDRESS_MATCH | (uint32_t) offset;
    }
    if (value >= arena_begin && value - arena_begin < match->memory.used) {
        offset = value - arena_begin;
        if (offset > MSL_RELOC_ADDRESS_OFFSET_MASK) {
            abort();
        }
        return (uint32_t) offset;
    }
    abort();
}

const MslRelocRecord* msl_reloc_records(const MslCoreMatch* match)
{
    if (match == NULL || match->memory.arena == NULL ||
        match->relocation_count == 0)
    {
        return NULL;
    }
    return (const MslRelocRecord*)
        (match->memory.arena + match->relocation_records_offset);
}

void* msl_reloc_record_address(const MslCoreMatch* match,
                               const MslRelocRecord* record)
{
    uint32_t offset;
    if (match == NULL || record == NULL) {
        return NULL;
    }
    offset = record->address & MSL_RELOC_ADDRESS_OFFSET_MASK;
    if ((record->address & MSL_RELOC_ADDRESS_MATCH) != 0) {
        return (uint8_t*) (uintptr_t) match + offset;
    }
    return match->memory.arena + offset;
}

size_t msl_reloc_resident_bytes(const MslCoreMatch* match)
{
    if (match == NULL) {
        return 0;
    }
    return (size_t) match->relocation_record_capacity *
               sizeof(MslRelocRecord) +
           (size_t) match->relocation_index_capacity * sizeof(uint16_t);
}

static uint16_t* compact_find_index_slot(MslCoreMatch* match,
                                         uint32_t address)
{
    MslRelocRecord* records = (MslRelocRecord*) msl_reloc_records(match);
    uint16_t* index = (uint16_t*)
        (match->memory.arena + match->relocation_index_offset);
    uint32_t mask = match->relocation_index_capacity - 1;
    uint32_t slot = compact_address_hash(address, mask);
    for (;;) {
        uint16_t entry = index[slot];
        if (entry == 0 || records[entry - 1].address == address) {
            return &index[slot];
        }
        slot = (slot + 1) & mask;
    }
}

void msl_reloc_begin_match(MslCoreMatch* match)
{
    if (match == NULL) {
        abort();
    }
    build_match = match;
    build_registry.count = 0;
    memset(build_registry.index, 0, sizeof(build_registry.index));
    match->relocation_records_offset = 0;
    match->relocation_index_offset = 0;
    match->relocation_count = 0;
    match->relocation_record_capacity = 0;
    match->relocation_index_capacity = 0;
}

int msl_reloc_seal_match(MslCoreMatch* match)
{
    MslRelocRecord* records;
    uint16_t* index;
    uint8_t* storage;
    size_t record_bytes;
    size_t index_bytes;
    size_t storage_bytes;
    uint32_t record_capacity;
    uint32_t index_capacity = 1;
    uint32_t i;

    if (match == NULL || build_match != match || build_registry.count == 0 ||
        build_registry.count >= UINT16_MAX)
    {
        return -1;
    }
    record_capacity = build_registry.count + MSL_RELOC_RUNTIME_RECORD_RESERVE;
    if (record_capacity > MSL_RELOC_OBJECT_CAPACITY) {
        record_capacity = MSL_RELOC_OBJECT_CAPACITY;
    }
    while (index_capacity < record_capacity * 2) {
        index_capacity *= 2;
    }
    record_bytes = (size_t) record_capacity * sizeof(MslRelocRecord);
    index_bytes = (size_t) index_capacity * sizeof(uint16_t);
    storage_bytes = record_bytes + index_bytes;
    storage = msl_memory_alloc(&match->memory, storage_bytes);
    if (storage == NULL) {
        return -1;
    }
    match->relocation_records_offset =
        (uint32_t) (storage - match->memory.arena);
    match->relocation_index_offset =
        match->relocation_records_offset + (uint32_t) record_bytes;
    match->relocation_count = build_registry.count;
    match->relocation_record_capacity = record_capacity;
    match->relocation_index_capacity = index_capacity;
    records = (MslRelocRecord*) storage;
    index = (uint16_t*) (storage + record_bytes);

    for (i = 0; i < build_registry.count; ++i) {
        const MslRelocObject* source = &build_registry.objects[i];
        uint16_t* slot;
        if (source->type >= UINT8_MAX || source->flags >= UINT8_MAX ||
            source->count >= UINT16_MAX)
        {
            return -1;
        }
        records[i].address = encode_address(match, source->address);
        records[i].stride = source->stride;
        records[i].count = (uint16_t) source->count;
        records[i].type = (uint8_t) source->type;
        records[i].flags = (uint8_t) source->flags;
        slot = compact_find_index_slot(match, records[i].address);
        if (*slot != 0) {
            return -1;
        }
        *slot = (uint16_t) (i + 1);
    }
    return 0;
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
    if (match->memory.sealed) {
        uint32_t encoded = encode_address(match, address);
        uint16_t* compact_slot = compact_find_index_slot(match, encoded);
        MslRelocRecord* records =
            (MslRelocRecord*) msl_reloc_records(match);
        MslRelocRecord* record;
        if (type >= UINT8_MAX || flags >= UINT8_MAX || count >= UINT16_MAX)
        {
            abort();
        }
        if (*compact_slot == 0) {
            if (match->relocation_count ==
                match->relocation_record_capacity)
            {
                fprintf(stderr,
                        "Melee core runtime relocation reserve exhausted: "
                        "records=%u capacity=%u address=%08x type=%u\n",
                        match->relocation_count,
                        match->relocation_record_capacity, encoded, type);
                abort();
            }
            record = &records[match->relocation_count];
            record->address = encoded;
            *compact_slot = (uint16_t) (match->relocation_count + 1);
            ++match->relocation_count;
        } else {
            record = &records[*compact_slot - 1];
        }
        record->type = (uint8_t) type;
        record->count = (uint16_t) count;
        record->stride = stride;
        record->flags = (uint8_t) flags;
        return;
    }
    if (build_match != match) {
        abort();
    }
    registry = &build_registry;
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
