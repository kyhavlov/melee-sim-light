#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_WASM)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "runtime/savestate.h"

#include "runtime/context.h"
#include "runtime/relocation.h"
#include "runtime/scalar.h"

#include <stdint.h>
#include <string.h>
#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_WASM)
#include <link.h>
#endif

enum {
    MSL_SAVESTATE_VERSION = 4,
};

typedef struct MslSavestateHeader {
    uint8_t magic[8];
    uint32_t version;
    uint32_t pointer_size;
    uint64_t layout_hash;
    uint64_t game_data_fingerprint;
    uint64_t artifact_hash;
    uint64_t match_size;
    uint64_t arena_used;
    uint64_t source_match;
    uint64_t source_match_arena;
    uint64_t source_game_data;
    uint64_t source_game_arena;
    uint64_t source_game_arena_size;
    uint64_t source_native_dat;
    uint64_t source_native_dat_size;
    uint64_t source_image;
    uint64_t source_image_size;
} MslSavestateHeader;

static const uint8_t savestate_magic[8] = {
    'M', 'S', 'L', 'S', 'S', '0', '4', 0,
};

typedef struct RelocBases {
    uintptr_t source_match;
    uintptr_t destination_match;
    uintptr_t source_match_arena;
    uintptr_t destination_match_arena;
    size_t match_arena_size;
    uintptr_t source_game_data;
    uintptr_t destination_game_data;
    uintptr_t source_game_arena;
    uintptr_t destination_game_arena;
    size_t game_arena_size;
    uintptr_t source_native_dat;
    uintptr_t destination_native_dat;
    size_t native_dat_size;
    uintptr_t source_image;
    uintptr_t destination_image;
    size_t image_size;
} RelocBases;

#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_WASM)
typedef struct ImageRangeQuery {
    uintptr_t anchor;
    uintptr_t base;
    size_t size;
} ImageRangeQuery;

static int find_image_range(struct dl_phdr_info* info, size_t size, void* data)
{
    ImageRangeQuery* query = data;
    uintptr_t begin = UINTPTR_MAX;
    uintptr_t end = 0;
    ElfW(Half) i;
    (void) size;

    for (i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)* segment = &info->dlpi_phdr[i];
        uintptr_t segment_begin;
        uintptr_t segment_end;
        if (segment->p_type != PT_LOAD || segment->p_memsz == 0) {
            continue;
        }
        segment_begin = (uintptr_t) info->dlpi_addr + segment->p_vaddr;
        segment_end = segment_begin + segment->p_memsz;
        if (segment_begin < begin) {
            begin = segment_begin;
        }
        if (segment_end > end) {
            end = segment_end;
        }
    }
    if (query->anchor < begin || query->anchor >= end) {
        return 0;
    }
    query->base = begin;
    query->size = end - begin;
    return 1;
}
#endif

static void current_image_range(uintptr_t* base, size_t* size)
{
    *base = 0;
    *size = 0;
#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_WASM)
    {
        ImageRangeQuery query = {
            .anchor = (uintptr_t) savestate_magic,
        };
        if (dl_iterate_phdr(find_image_range, &query) == 0 ||
            query.base == 0 || query.size == 0)
        {
            return;
        }
        *base = query.base;
        *size = query.size;
    }
#endif
}

static uint64_t hash_bytes(uint64_t hash, const void* data, size_t size)
{
    const uint8_t* bytes = data;
    while (size-- != 0) {
        hash = (hash ^ *bytes++) * UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t layout_hash(void)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t type;
    size_t match_size = sizeof(MslCoreMatch);
    hash = hash_bytes(hash, &match_size, sizeof(match_size));
    for (type = 0; type < MSL_RELOC_TYPE_COUNT; ++type) {
        const MslRelocTypeDesc* desc = &msl_reloc_type_descs[type];
        hash = hash_bytes(hash, &desc->size, sizeof(desc->size));
        hash = hash_bytes(hash, &desc->pointer_count,
                          sizeof(desc->pointer_count));
        hash = hash_bytes(hash, desc->pointer_offsets,
                          (size_t) desc->pointer_count * sizeof(uint32_t));
    }
    return hash;
}

static uint64_t artifact_hash(const MslSavestateHeader* source,
                              const void* match, const void* arena,
                              size_t arena_used)
{
    MslSavestateHeader header = *source;
    uint64_t hash = UINT64_C(1469598103934665603);
    header.artifact_hash = 0;
    hash = hash_bytes(hash, &header, sizeof(header));
    hash = hash_bytes(hash, match, (size_t) header.match_size);
    return hash_bytes(hash, arena, arena_used);
}

static uintptr_t map_range(uintptr_t value, uintptr_t source, size_t size,
                           uintptr_t destination)
{
    if (source != 0 && value >= source && value - source < size) {
        return destination + (value - source);
    }
    return value;
}

static uintptr_t relocate_value(uintptr_t value, const RelocBases* bases)
{
    uintptr_t result;
    result = map_range(value, bases->source_match, sizeof(MslCoreMatch),
                       bases->destination_match);
    if (result != value) {
        return result;
    }
    result =
        map_range(value, bases->source_match_arena, bases->match_arena_size,
                  bases->destination_match_arena);
    if (result != value) {
        return result;
    }
    result = map_range(value, bases->source_game_data, sizeof(MslCoreGameData),
                       bases->destination_game_data);
    if (result != value) {
        return result;
    }
    result = map_range(value, bases->source_game_arena, bases->game_arena_size,
                       bases->destination_game_arena);
    if (result != value) {
        return result;
    }
    result = map_range(value, bases->source_native_dat, bases->native_dat_size,
                       bases->destination_native_dat);
    if (result != value) {
        return result;
    }
    // Persistent callbacks, HSD class descriptors, and compile-time tables
    // live in the core image rather than Match or GameData. Rebase their
    // typed pointer slots by the ELF image load bias so a same-build artifact
    // does not depend on ASLR or the originating process.
    return map_range(value, bases->source_image, bases->image_size,
                     bases->destination_image);
}

static void relocate_slot(void* address, const RelocBases* bases)
{
    uintptr_t value;
    memcpy(&value, address, sizeof(value));
    value = relocate_value(value, bases);
    memcpy(address, &value, sizeof(value));
}

static void relocate_typed_object(void* address, uint32_t type,
                                  uint32_t count, uint32_t stride,
                                  uint32_t flags, const RelocBases* bases)
{
    uint32_t element;
    if (type == MSL_RELOC_POINTER_ARRAY) {
        for (element = 0; element < count; ++element) {
            relocate_slot(
                (uint8_t*) address + (size_t) element * stride, bases);
        }
    } else if (type < MSL_RELOC_TYPE_COUNT && type != MSL_RELOC_RAW)
    {
        const MslRelocTypeDesc* desc = &msl_reloc_type_descs[type];
        for (element = 0; element < count; ++element) {
            uint8_t* base =
                (uint8_t*) address + (size_t) element * stride;
            uint32_t field;
            for (field = 0; field < desc->pointer_count; ++field) {
                if (desc->pointer_offsets[field] + sizeof(void*) <= stride)
                {
                    relocate_slot(base + desc->pointer_offsets[field], bases);
                }
            }
        }
    }
    if ((flags & MSL_RELOC_INTRUSIVE_FIRST_POINTER) != 0) {
        relocate_slot(address, bases);
    }
}

static int relocate_match(MslCoreMatch* match, const RelocBases* bases)
{
    const MslRelocTypeDesc* root = &msl_reloc_type_descs[MSL_RELOC_MATCH];
    const MslRelocRecord* records;
    size_t record_bytes;
    size_t index_bytes;
    uint32_t i;

    record_bytes = (size_t) match->relocation_record_capacity *
                   sizeof(MslRelocRecord);
    index_bytes = (size_t) match->relocation_index_capacity * sizeof(uint16_t);
    if (match->relocation_count == 0 ||
        match->relocation_record_capacity == 0 ||
        match->relocation_count > match->relocation_record_capacity ||
        match->relocation_index_capacity == 0 ||
        (match->relocation_index_capacity &
         (match->relocation_index_capacity - 1)) != 0 ||
        match->relocation_records_offset > bases->match_arena_size ||
        record_bytes > bases->match_arena_size -
                           match->relocation_records_offset ||
        match->relocation_index_offset !=
            match->relocation_records_offset + record_bytes ||
        match->relocation_index_offset > bases->match_arena_size ||
        index_bytes > bases->match_arena_size - match->relocation_index_offset)
    {
        return -1;
    }
    records = (const MslRelocRecord*)
        (bases->destination_match_arena +
         match->relocation_records_offset);
    for (i = 0; i < match->relocation_count; ++i) {
        const MslRelocRecord* record = &records[i];
        uintptr_t destination;
        uint32_t offset =
            record->address & MSL_RELOC_ADDRESS_OFFSET_MASK;
        if ((record->address & MSL_RELOC_ADDRESS_MATCH) != 0) {
            if (offset > sizeof(*match) - sizeof(void*)) {
                return -1;
            }
            destination = bases->destination_match + offset;
        } else {
            if (offset > bases->match_arena_size - sizeof(void*)) {
                return -1;
            }
            destination = bases->destination_match_arena + offset;
        }
        relocate_typed_object((void*) destination, record->type,
                              record->count, record->stride, record->flags,
                              bases);
    }
    for (i = 0; i < root->pointer_count; ++i) {
        relocate_slot((uint8_t*) match + root->pointer_offsets[i], bases);
    }
    match->memory.arena = (uint8_t*) bases->destination_match_arena;
    match->memory.allocations = NULL;
    match->memory.allocation_capacity = 0;
    return 0;
}

static void fill_header(MslSavestateHeader* header, const MslCoreMatch* match)
{
    uintptr_t image_base;
    size_t image_size;

    memset(header, 0, sizeof(*header));
    memcpy(header->magic, savestate_magic, sizeof(header->magic));
    header->version = MSL_SAVESTATE_VERSION;
    header->pointer_size = sizeof(void*);
    header->layout_hash = layout_hash();
    header->game_data_fingerprint = match->game_data->fingerprint;
    header->match_size = sizeof(*match);
    header->arena_used = match->memory.used;
    header->source_match = (uintptr_t) match;
    header->source_match_arena = (uintptr_t) match->memory.arena;
    header->source_game_data = (uintptr_t) match->game_data;
    header->source_game_arena = (uintptr_t) match->game_data->memory.arena;
    header->source_game_arena_size = match->game_data->memory.used;
#ifdef MSL_CORE_NATIVE
    header->source_native_dat = (uintptr_t) match->game_data->native_dat.arena;
    header->source_native_dat_size = match->game_data->native_dat.arena_used;
#endif
    current_image_range(&image_base, &image_size);
    header->source_image = image_base;
    header->source_image_size = image_size;
}

static void fill_bases(RelocBases* bases, const MslSavestateHeader* header,
                       MslCoreMatch* destination)
{
    uintptr_t image_base;
    size_t image_size;

    memset(bases, 0, sizeof(*bases));
    bases->source_match = header->source_match;
    bases->destination_match = (uintptr_t) destination;
    bases->source_match_arena = header->source_match_arena;
    bases->destination_match_arena = (uintptr_t) destination->memory.arena;
    bases->match_arena_size = header->arena_used;
    bases->source_game_data = header->source_game_data;
    bases->destination_game_data = (uintptr_t) destination->game_data;
    bases->source_game_arena = header->source_game_arena;
    bases->destination_game_arena =
        (uintptr_t) destination->game_data->memory.arena;
    bases->game_arena_size = header->source_game_arena_size;
    bases->source_native_dat = header->source_native_dat;
#ifdef MSL_CORE_NATIVE
    bases->destination_native_dat =
        (uintptr_t) destination->game_data->native_dat.arena;
#endif
    bases->native_dat_size = header->source_native_dat_size;
    current_image_range(&image_base, &image_size);
    bases->source_image = header->source_image;
    bases->destination_image = image_base;
    bases->image_size = image_size;
}

size_t msl_core_match_save_size(const MslCoreMatch* match)
{
    return match != NULL ? sizeof(MslSavestateHeader) + sizeof(*match) +
                               match->memory.used
                         : 0;
}

int msl_core_match_save(const MslCoreMatch* match, void* buffer,
                        size_t buffer_size, size_t* written)
{
    MslSavestateHeader header;
    size_t required;
    if (match == NULL || buffer == NULL || match->game_data == NULL) {
        return -1;
    }
    required = msl_core_match_save_size(match);
    if (buffer_size < required) {
        return -1;
    }
    fill_header(&header, match);
#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_WASM)
    if (header.source_image == 0 || header.source_image_size == 0) {
        return -1;
    }
#endif
    header.artifact_hash =
        artifact_hash(&header, match, match->memory.arena, match->memory.used);
    memcpy(buffer, &header, sizeof(header));
    memcpy((uint8_t*) buffer + sizeof(header), match, sizeof(*match));
    memcpy((uint8_t*) buffer + sizeof(header) + sizeof(*match),
           match->memory.arena, match->memory.used);
    if (written != NULL) {
        *written = required;
    }
    return 0;
}

static int compatible_header(const MslSavestateHeader* header,
                             size_t buffer_size,
                             const MslCoreMatch* destination,
                             const void* buffer)
{
    const uint8_t* bytes = buffer;
    size_t prefix = sizeof(*header) + sizeof(MslCoreMatch);
    size_t required;
    uintptr_t image_base;
    size_t image_size;
    current_image_range(&image_base, &image_size);
    if (memcmp(header->magic, savestate_magic, sizeof(header->magic)) != 0 ||
        header->version != MSL_SAVESTATE_VERSION ||
        header->pointer_size != sizeof(void*) ||
        header->layout_hash != layout_hash() ||
        header->match_size != sizeof(MslCoreMatch) ||
        destination->game_data == NULL ||
        header->game_data_fingerprint != destination->game_data->fingerprint ||
        header->source_game_arena_size !=
            destination->game_data->memory.used ||
        ((header->source_image == 0) != (image_base == 0)) ||
        header->source_image_size != image_size ||
        header->arena_used > destination->memory.capacity ||
        header->arena_used > SIZE_MAX - prefix
#ifdef MSL_CORE_NATIVE
        || header->source_native_dat_size !=
               destination->game_data->native_dat.arena_used
#else
        || header->source_native_dat_size != 0
#endif
    )
    {
        return 0;
    }
    required = prefix + (size_t) header->arena_used;
    if (required > buffer_size) {
        return 0;
    }
    return header->artifact_hash ==
           artifact_hash(header, bytes + sizeof(*header), bytes + prefix,
                         (size_t) header->arena_used);
}

int msl_core_match_restore(MslCoreMatch* match, const void* buffer,
                           size_t buffer_size)
{
    MslSavestateHeader header;
    RelocBases bases;
    const MslCoreGameData* game_data;
    uint8_t* arena;
    size_t capacity;
    if (match == NULL || buffer == NULL || buffer_size < sizeof(header)) {
        return -1;
    }
    memcpy(&header, buffer, sizeof(header));
    if (!compatible_header(&header, buffer_size, match, buffer)) {
        return -1;
    }
    game_data = match->game_data;
    arena = match->memory.arena;
    capacity = match->memory.capacity;
    // Capture destination bases before copying the source image. Do not seed
    // destination pointers into that image before relocation: a fresh host
    // allocation may numerically overlap one of the old source ranges and be
    // mistaken for a source pointer. Every pointer visited below must still
    // carry source provenance.
    fill_bases(&bases, &header, match);
    memcpy(arena, (const uint8_t*) buffer + sizeof(header) + sizeof(*match),
           header.arena_used);
    memcpy(match, (const uint8_t*) buffer + sizeof(header), sizeof(*match));
    if (relocate_match(match, &bases) != 0) {
        return -1;
    }
    match->game_data = game_data;
    match->memory.arena = arena;
    match->memory.capacity = capacity;
    msl_core_bind_match(match);
    return 0;
}

int msl_core_match_copy(MslCoreMatch* destination, const MslCoreMatch* source)
{
    MslSavestateHeader header;
    RelocBases bases;
    const MslCoreGameData* game_data;
    uint8_t* arena;
    size_t capacity;
    if (destination == NULL || source == NULL ||
        destination->game_data == NULL || source->game_data == NULL ||
        destination->game_data->fingerprint !=
            source->game_data->fingerprint ||
        source->memory.used > destination->memory.capacity)
    {
        return -1;
    }
    if (destination == source) {
        return 0;
    }
    fill_header(&header, source);
    game_data = destination->game_data;
    arena = destination->memory.arena;
    capacity = destination->memory.capacity;
    fill_bases(&bases, &header, destination);
    memcpy(arena, source->memory.arena, source->memory.used);
    memcpy(destination, source, sizeof(*destination));
    if (relocate_match(destination, &bases) != 0) {
        return -1;
    }
    destination->game_data = game_data;
    destination->memory.arena = arena;
    destination->memory.capacity = capacity;
    msl_core_bind_match(destination);
    return 0;
}
