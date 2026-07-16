#ifndef MSL_CORE_RUNTIME_RELOCATION_H
#define MSL_CORE_RUNTIME_RELOCATION_H

#include <stddef.h>
#include <stdint.h>

typedef enum MslRelocType {
    MSL_RELOC_RAW = 0,
    MSL_RELOC_POINTER_ARRAY,
#define MSL_RELOC_TYPE(id, type) MSL_RELOC_##id,
#include "runtime/relocation_types.def"
#undef MSL_RELOC_TYPE
    MSL_RELOC_TYPE_COUNT,
} MslRelocType;

typedef struct MslRelocTypeDesc {
    const uint32_t* pointer_offsets;
    uint32_t pointer_count;
    uint32_t size;
} MslRelocTypeDesc;

extern const MslRelocTypeDesc msl_reloc_type_descs[MSL_RELOC_TYPE_COUNT];

enum {
    MSL_RELOC_OBJECT_CAPACITY = 16384,
    MSL_RELOC_INDEX_CAPACITY = 32768,
    // Source intrusive allocators use the first pointer-sized word as their
    // free-list link even when the allocated type's first field is scalar.
    MSL_RELOC_INTRUSIVE_FIRST_POINTER = 1 << 0,
};

typedef struct MslRelocObject {
    void* address;
    uint32_t type;
    uint32_t count;
    uint32_t stride;
    uint32_t flags;
} MslRelocObject;

typedef struct MslRelocRegistry {
    MslRelocObject objects[MSL_RELOC_OBJECT_CAPACITY];
    // Open-addressed object index; zero is empty and entries store index + 1.
    // Iteration and savestate order remain the source allocation order above.
    uint32_t index[MSL_RELOC_INDEX_CAPACITY];
    uint32_t count;
} MslRelocRegistry;

void msl_reloc_register(void* address, MslRelocType type, uint32_t count,
                        uint32_t stride, uint32_t flags);
typedef struct _HSD_ClassInfo HSD_ClassInfo;
void msl_reloc_register_hsd_class(void* address, HSD_ClassInfo* info);
void msl_reloc_rebuild_index(MslRelocRegistry* registry);

#endif
