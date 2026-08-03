#include "platform/native_dat.h"
#include "platform/memory.h"
#include "runtime/context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <baselib/jobj.h>
#include <baselib/id.h>
#include <baselib/memory.h>
#include <baselib/psstructs.h>
#include <melee/ft/types.h>
#include <melee/gr/ground.h>
#include <melee/it/it_3F14.h>
#include <melee/it/types.h>
#ifndef MSL_CORE_WASM
#include <sys/mman.h>
#endif

// HSD DAT stores a 32-bit big-endian graph. On retail/PPC,
// HSD_ArchiveParse relocates that graph in place. A 64-bit little-endian
// process instead materializes native objects once at archive initialization.
// Source: refs/melee/src/sysdolphin/baselib/archive.c::HSD_ArchiveParse.

typedef struct MslDatMemo {
    uint32_t source_offset;
    const MslDatType* type;
    void* native;
} MslDatMemo;

typedef struct MslNativePublic {
    char* symbol;
    void* address;
} MslNativePublic;

typedef struct MslNativeArchive {
    HSD_Archive* archive;
    uint8_t* source;
    uint8_t* data;
    uint32_t data_size;
    uint32_t* reloc_fields;
    uint32_t reloc_count;
    uint32_t* boundaries;
    uint32_t boundary_count;
    MslDatMemo* memo;
    uint32_t memo_count;
    uint32_t memo_capacity;
    uint8_t* command_words;
    const MslDatType* command_word_type;
    MslNativePublic* publics;
    uint32_t public_count;
    uint32_t public_capacity;
} MslNativeArchive;

enum {
    // Native graph nodes contain widened pointers and HSD data may request
    // 32-byte alignment. No per-type padding is required.
    MSL_NATIVE_DAT_ARENA_ALIGN = 32,
};

#define native_archive_cache (msl_core_native_dat_context()->archive_cache)
#define native_archive_cache_count                                            \
    (msl_core_native_dat_context()->archive_cache_count)
#define native_dat_arena (msl_core_native_dat_context()->arena)
#define native_dat_arena_used (msl_core_native_dat_context()->arena_used)
#define native_dat_initialization_complete                                    \
    (msl_core_native_dat_context()->initialization_complete)

enum {
    DW_ATE_BOOLEAN = 2,
    DW_ATE_FLOAT = 4,
    DW_ATE_SIGNED = 5,
    DW_ATE_SIGNED_CHAR = 6,
};

static uint16_t read_be16(const void* source)
{
    const uint8_t* bytes = source;
    return (uint16_t) ((uint16_t) bytes[0] << 8 | bytes[1]);
}

static uint32_t read_be32(const void* source)
{
    const uint8_t* bytes = source;
    return (uint32_t) bytes[0] << 24 | (uint32_t) bytes[1] << 16 |
           (uint32_t) bytes[2] << 8 | bytes[3];
}

static uint64_t read_be64(const void* source)
{
    return (uint64_t) read_be32(source) << 32 |
           read_be32((const uint8_t*) source + 4);
}

static int compare_u32(const void* lhs, const void* rhs)
{
    uint32_t a = *(const uint32_t*) lhs;
    uint32_t b = *(const uint32_t*) rhs;
    return a < b ? -1 : a > b;
}

static void* native_alloc(size_t size)
{
    size_t aligned;
    void* result;

    if (native_dat_initialization_complete) {
        fprintf(stderr,
                "native DAT translation reached after match initialization\n");
        abort();
    }
    if (native_dat_arena == NULL) {
#ifdef MSL_CORE_WASM
        native_dat_arena = malloc(MSL_NATIVE_DAT_ARENA_BYTES);
        if (native_dat_arena == NULL) {
            fprintf(stderr, "native DAT arena reservation failed\n");
            abort();
        }
#else
        void* mapping = mmap(NULL, MSL_NATIVE_DAT_ARENA_BYTES,
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            fprintf(stderr, "native DAT arena reservation failed\n");
            abort();
        }
        native_dat_arena = mapping;
#endif
    }
    if (size == 0) {
        size = 1;
    }
    if (size > MSL_NATIVE_DAT_ARENA_BYTES - MSL_NATIVE_DAT_ARENA_ALIGN) {
        fprintf(stderr, "native DAT allocation is too large: %zu bytes\n",
                size);
        abort();
    }
    aligned = (size + MSL_NATIVE_DAT_ARENA_ALIGN - 1) &
              ~(size_t) (MSL_NATIVE_DAT_ARENA_ALIGN - 1);
    if (aligned > MSL_NATIVE_DAT_ARENA_BYTES - native_dat_arena_used) {
        fprintf(stderr, "native DAT arena exhausted: request=%zu used=%zu/%u\n",
                size, native_dat_arena_used, MSL_NATIVE_DAT_ARENA_BYTES);
        abort();
    }
    result = native_dat_arena + native_dat_arena_used;
    native_dat_arena_used += aligned;
    memset(result, 0, size);
    return result;
}

static void require_dat_initialization(const char* operation)
{
    if (native_dat_initialization_complete) {
        fprintf(stderr, "%s reached after native DAT initialization\n",
                operation);
        abort();
    }
}

void msl_native_dat_for_each_figa(void (*visit)(FigaTree*, void*),
                                  void* visit_context)
{
    uint32_t archive_index;
    HSD_ASSERT(171, visit != NULL);
    for (archive_index = 0; archive_index < native_archive_cache_count;
         ++archive_index)
    {
        MslNativeArchive* archive =
            native_archive_cache[archive_index].archive.top_ptr;
        uint32_t public_index;
        for (public_index = 0; public_index < archive->public_count;
             ++public_index)
        {
            MslNativePublic* public = &archive->publics[public_index];
            size_t length = strlen(public->symbol);
            if (length >= 9 &&
                strcmp(public->symbol + length - 9, "_figatree") == 0)
            {
                visit(public->address, visit_context);
            }
        }
    }
}

void msl_native_dat_finish_initialization(void)
{
    const size_t page_size = 4096;
    size_t protected_size;
    uint32_t i;

    if (native_dat_initialization_complete) {
        return;
    }
    for (i = 0; i < native_archive_cache_count; ++i) {
        // The runtime graph and byte streams live in the native arena. Make
        // original big-endian file buffers inaccessible so a leaked raw
        // pointer or per-frame archive read fails immediately.
        msl_memory_protect_allocation(native_archive_cache[i].source);
    }
    protected_size =
        (native_dat_arena_used + page_size - 1) & ~(page_size - 1);
#ifdef MSL_CORE_WASM
    // Wasm linear memory has no page-level read-only mapping. The logical seal
    // below still rejects every later DAT allocation/translation attempt.
    (void) protected_size;
#else
    if (protected_size != 0 &&
        mprotect(native_dat_arena, protected_size, PROT_READ) != 0)
    {
        perror("mprotect immutable native DAT arena");
        abort();
    }
#endif
    native_dat_initialization_complete = 1;
}

int msl_native_dat_owns(const void* pointer)
{
    uintptr_t address = (uintptr_t) pointer;
    uintptr_t begin = (uintptr_t) native_dat_arena;
    return native_dat_arena != NULL && address >= begin &&
           address - begin < native_dat_arena_used;
}

void msl_native_dat_context_destroy(MslNativeDatContext* context)
{
    if (context == NULL || context->arena == NULL) {
        return;
    }
#ifdef MSL_CORE_WASM
    free(context->arena);
#else
    munmap(context->arena, MSL_NATIVE_DAT_ARENA_BYTES);
#endif
    memset(context, 0, sizeof(*context));
}

static int contains_u32(const uint32_t* values, uint32_t count, uint32_t value)
{
    uint32_t lo = 0;
    uint32_t hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (values[mid] < value) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < count && values[lo] == value;
}

static uint32_t next_boundary(const MslNativeArchive* context,
                              uint32_t offset)
{
    uint32_t lo = 0;
    uint32_t hi = context->boundary_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (context->boundaries[mid] <= offset) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < context->boundary_count ? context->boundaries[lo]
                                        : context->data_size;
}

static MslNativeArchive* archive_context(HSD_Archive* archive)
{
    return (MslNativeArchive*) archive->top_ptr;
}

static uint32_t raw_public_offset(HSD_Archive* archive, const char* symbol)
{
    uint32_t i;
    for (i = 0; i < archive->header.nb_public; ++i) {
        if (strcmp(archive->symbols + archive->public_info[i].symbol, symbol) ==
            0)
        {
            return archive->public_info[i].offset;
        }
    }
    return UINT32_MAX;
}

static uint32_t raw_pointer(const MslNativeArchive* context,
                            uint32_t field_offset)
{
    if (field_offset + 4 > context->data_size ||
        !contains_u32(context->reloc_fields, context->reloc_count,
                      field_offset))
    {
        return UINT32_MAX;
    }
    return read_be32(context->data + field_offset);
}

static void translate_value(MslNativeArchive* context,
                            const MslDatType* type, uint32_t source_offset,
                            void* native);

static MslDatMemo* find_memo(MslNativeArchive* context, uint32_t source_offset)
{
    uint32_t i;
    for (i = 0; i < context->memo_count; ++i) {
        if (context->memo[i].source_offset == source_offset) {
            return &context->memo[i];
        }
    }
    return NULL;
}

static void add_memo(MslNativeArchive* context, uint32_t source_offset,
                     const MslDatType* type, void* native)
{
    if (context->memo_count == context->memo_capacity) {
        uint32_t capacity = context->memo_capacity == 0
                                ? 256
                                : context->memo_capacity * 2;
        MslDatMemo* replacement = native_alloc(
            (size_t) capacity * sizeof(*replacement));
        if (context->memo != NULL) {
            memcpy(replacement, context->memo,
                   (size_t) context->memo_count * sizeof(*replacement));
        }
        context->memo = replacement;
        context->memo_capacity = capacity;
    }
    context->memo[context->memo_count++] =
        (MslDatMemo) { source_offset, type, native };
}

static void* translate_command_address(MslNativeArchive* context,
                                       uint32_t source_offset,
                                       const MslDatType* type)
{
    uint32_t count;
    uint32_t i;

    if (type->source_size != 4 || type->native_size < sizeof(void*) ||
        source_offset % type->source_size != 0)
    {
        fprintf(stderr, "native DAT has an invalid command-word layout\n");
        abort();
    }
    count = (context->data_size + type->source_size - 1) / type->source_size;
    if (context->command_words == NULL) {
        context->command_words =
            native_alloc((size_t) count * type->native_size);
        context->command_word_type = type;

        // A fighter script may share a suffix with another script, so a
        // relocation target can lie inside a sequential command stream. A
        // boundary-sized translation would split that stream into unrelated
        // allocations once native pointers widen CmdUnion from four to eight
        // bytes. Mirror the archive's source-word address space once instead:
        // sequential cursor increments and every interior control-flow target
        // then retain the retail topology exactly.
        // refs/melee/src/melee/lb/{types.h,lbcommand.c}
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
        for (i = 0; i < count; ++i) {
            uint32_t offset = i * type->source_size;
            uint32_t remaining = context->data_size - offset;
            uint32_t size = remaining < type->source_size ? remaining
                                                          : type->source_size;
            memcpy(context->command_words + (size_t) i * type->native_size,
                   context->data + offset, size);
        }
        for (i = 0; i < context->reloc_count; ++i) {
            uint32_t field = context->reloc_fields[i];
            uint32_t target = read_be32(context->data + field);
            void* pointer;

            if (field % type->source_size != 0 ||
                target >= context->data_size ||
                target % type->source_size != 0)
            {
                continue;
            }
            pointer = context->command_words +
                      (size_t) (target / type->source_size) *
                          type->native_size;
            memcpy(context->command_words +
                       (size_t) (field / type->source_size) *
                           type->native_size,
                   &pointer, sizeof(pointer));
        }
    } else if (context->command_word_type != type) {
        fprintf(stderr, "native DAT command word requested with two types\n");
        abort();
    }
    return context->command_words +
           (size_t) (source_offset / type->source_size) * type->native_size;
}

static void* translate_target_count(MslNativeArchive* context,
                                    uint32_t source_offset,
                                    const MslDatType* type, uint32_t count)
{
    MslDatMemo* memo;
    uint8_t* result;
    uint32_t i;

    if (source_offset >= context->data_size) {
        fprintf(stderr, "native DAT pointer %x is outside %x-byte body\n",
                source_offset, context->data_size);
        abort();
    }
    if (strcmp(type->name, "CmdUnion") == 0) {
        return translate_command_address(context, source_offset, type);
    }
    memo = find_memo(context, source_offset);
    if (memo != NULL) {
        if (memo->type != type) {
            fprintf(stderr,
                    "native DAT offset %x requested as both %s and %s\n",
                    source_offset, memo->type->name, type->name);
            abort();
        }
        return memo->native;
    }
    if (strcmp(type->name, "ItemStateArray") == 0) {
        const MslDatType* array;
        const MslDatType* element;
        uint32_t span;

        if (type->field_count != 1 ||
            (array = type->fields[0].type)->kind != MSL_DAT_ARRAY ||
            (element = array->element)->source_size == 0)
        {
            fprintf(stderr, "native DAT ItemStateArray lacks an element view\n");
            abort();
        }
        span = next_boundary(context, source_offset) - source_offset;
        if (span == 0 || span % element->source_size != 0) {
            fprintf(stderr,
                    "native DAT ItemStateArray has invalid %u-byte extent\n",
                    span);
            abort();
        }
        count = span / element->source_size;
        result = native_alloc((size_t) count * element->native_size);
        add_memo(context, source_offset, type, result);
        for (i = 0; i < count; ++i) {
            translate_value(context, element,
                            source_offset + i * element->source_size,
                            result + (size_t) i * element->native_size);
        }
        return result;
    }
    if (type->kind == MSL_DAT_VOID) {
        uint32_t end = next_boundary(context, source_offset);
        size_t size = end > source_offset ? end - source_offset : 1;
        result = native_alloc(size);
        add_memo(context, source_offset, type, result);
        memcpy(result, context->data + source_offset, size);
        return result;
    }
    if (count == 0) {
        count = 1;
    }
    if (type->native_size == 0 ||
        (size_t) count > SIZE_MAX / type->native_size)
    {
        fprintf(stderr, "invalid native DAT extent for %s\n", type->name);
        abort();
    }
    result = native_alloc((size_t) count * type->native_size);
    add_memo(context, source_offset, type, result);
    for (i = 0; i < count; ++i) {
        translate_value(context, type,
                        source_offset + i * type->source_size,
                        result + (size_t) i * type->native_size);
    }
    return result;
}

static void* translate_target(MslNativeArchive* context,
                              uint32_t source_offset,
                              const MslDatType* type)
{
    uint32_t span = next_boundary(context, source_offset) - source_offset;
    uint32_t count = 1;
    int graph_node =
        type->kind == MSL_DAT_STRUCT &&
        (strncmp(type->name, "HSD_", 4) == 0 ||
         strncmp(type->name, "_HSD_", 5) == 0);
    if (!graph_node && type->source_size != 0 && span >= type->source_size &&
        span % type->source_size == 0)
    {
        count = span / type->source_size;
    }
    return translate_target_count(context, source_offset, type, count);
}

static uint64_t read_unsigned(const uint8_t* source, uint32_t size)
{
    switch (size) {
    case 1: return source[0];
    case 2: return read_be16(source);
    case 4: return read_be32(source);
    case 8: return read_be64(source);
    default:
        fprintf(stderr, "unsupported native DAT scalar width %u\n", size);
        abort();
    }
}

static void write_integer(void* native, uint32_t size, uint64_t value)
{
    switch (size) {
    case 1: *(uint8_t*) native = (uint8_t) value; break;
    case 2: *(uint16_t*) native = (uint16_t) value; break;
    case 4: *(uint32_t*) native = (uint32_t) value; break;
    case 8: *(uint64_t*) native = value; break;
    default:
        fprintf(stderr, "unsupported native integer width %u\n", size);
        abort();
    }
}

static void translate_base(const MslDatType* type, const uint8_t* source,
                           void* native)
{
    uint64_t value = read_unsigned(source, type->source_size);
    if (type->base_encoding == DW_ATE_FLOAT) {
        if (type->source_size != type->native_size) {
            fprintf(stderr, "native DAT float width changed for %s\n",
                    type->name);
            abort();
        }
        write_integer(native, type->native_size, value);
        return;
    }
    if ((type->base_encoding == DW_ATE_SIGNED ||
         type->base_encoding == DW_ATE_SIGNED_CHAR) &&
        type->source_size < 8)
    {
        uint32_t bits = type->source_size * 8;
        value = (uint64_t) ((int64_t) (value << (64 - bits)) >> (64 - bits));
    }
    write_integer(native, type->native_size, value);
}

static void translate_bitfield(const MslDatType* type,
                               const uint8_t* source, uint8_t* native)
{
    uint64_t value = 0;
    uint32_t i;
    for (i = 0; i < type->bit_size; ++i) {
        uint32_t bit = type->source_bit_offset + i;
        value = value << 1 | ((source[bit / 8] >> (7 - bit % 8)) & 1U);
    }
    for (i = 0; i < type->bit_size; ++i) {
        uint32_t bit = type->native_bit_offset + i;
        uint8_t mask = (uint8_t) (1U << (bit % 8));
        if ((value >> i) & 1U) {
            native[bit / 8] |= mask;
        } else {
            native[bit / 8] &= (uint8_t) ~mask;
        }
    }
}

static void translate_value(MslNativeArchive* context,
                            const MslDatType* type, uint32_t source_offset,
                            void* native)
{
    uint32_t i;
    if (source_offset + type->source_size > context->data_size &&
        type->kind != MSL_DAT_VOID)
    {
        fprintf(stderr, "native DAT %s at %x exceeds body\n", type->name,
                source_offset);
        abort();
    }
    switch (type->kind) {
    case MSL_DAT_VOID: return;
    case MSL_DAT_BASE:
        translate_base(type, context->data + source_offset, native);
        return;
    case MSL_DAT_STRUCT:
        if (strcmp(type->name, "HSD_Joint") == 0) {
            uint32_t flags = read_be32(context->data + source_offset + 0x04);
            for (i = 0; i < type->field_count; ++i) {
                const MslDatField* field = &type->fields[i];
                translate_value(context, field->type,
                                source_offset + field->source_offset,
                                (uint8_t*) native + field->native_offset);
            }
            if ((flags & JOBJ_SPLINE) != 0) {
                uint32_t target = raw_pointer(context, source_offset + 0x10);
                void* spline = NULL;
                if (target != UINT32_MAX) {
                    spline = translate_target(context, target,
                                              msl_dat_root_HSD_Spline);
                }
                memcpy((uint8_t*) native + offsetof(HSD_Joint, u), &spline,
                       sizeof(spline));
            }
            // The shared DWARF view omits HSD_Joint's anonymous union. Spline
            // is gameplay-bearing through HSD_A_J_PATH; DObj and particle
            // variants remain presentation-only in this headless graph.
            // refs/melee/src/sysdolphin/baselib/{jobj.h,jobj.c::JObjUpdateFunc}
            return;
        }
        if (strcmp(type->name, "HSD_AObjDesc") == 0) {
            // AObjDesc::obj_id is declared u32 because retail uses either an
            // object-table id or a relocated HSD_Joint address in the same
            // word. When the DAT relocation table marks it, materialize the
            // Joint graph and retain its deterministic native-DAT offset id —
            // the same source-width identity used by the hosted HSD table.
            // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjLoadDesc
            // refs/melee/src/sysdolphin/baselib/jobj.c::HSD_JObjLoadJoint
            for (i = 0; i < type->field_count; ++i) {
                const MslDatField* field = &type->fields[i];
                uint32_t field_source = source_offset + field->source_offset;
                if (field->source_offset == 0x0C) {
                    uint32_t target = raw_pointer(context, field_source);
                    if (target != UINT32_MAX) {
                        void* joint = translate_target(
                            context, target, msl_dat_root_HSD_Joint);
                        write_integer((uint8_t*) native +
                                          field->native_offset,
                                      field->type->native_size,
                                      msl_hsd_id_from_pointer(joint));
                        continue;
                    }
                }
                translate_value(context, field->type, field_source,
                                (uint8_t*) native + field->native_offset);
            }
            return;
        }
        if (strcmp(type->name, "DynamicsDesc") == 0) {
            const MslDatType* source_element = NULL;
            uint32_t count;

            // Archive DynamicsDesc::data points at a packed array of
            // lb_00F9_UnkDesc1Inner records.  Its declared DynamicsData* type
            // is the runtime view used after lb_8000FD48 allocates a linked
            // list; lb_80011710 deliberately recovers the packed source view
            // and indexes it through the archive-owned count.  Translating a
            // widened DynamicsData would split that packed array at the PPC
            // union/pointer boundary, so materialize the actual source view.
            // refs/melee/src/melee/lb/lbspdisplay.c::{lb_8000FD48,lb_80011710}
            count = read_be32(context->data + source_offset +
                              type->fields[1].source_offset);
            for (i = 0; i < type->field_count; ++i) {
                const MslDatField* field = &type->fields[i];
                if (field->type->kind == MSL_DAT_POINTER) {
                    const MslDatType* dynamics_data = field->type->element;
                    uint32_t j;
                    for (j = 0; j < dynamics_data->field_count; ++j) {
                        const MslDatType* candidate =
                            dynamics_data->fields[j].type;
                        uint32_t k;
                        if (strcmp(candidate->name, "PolymorphicDesc") != 0) {
                            continue;
                        }
                        for (k = 0; k < candidate->field_count; ++k) {
                            const MslDatType* view = candidate->fields[k].type;
                            if (strcmp(view->name, "lb_00F9_UnkDesc1") == 0 &&
                                view->field_count == 1 &&
                                view->fields[0].type->kind == MSL_DAT_ARRAY)
                            {
                                source_element =
                                    view->fields[0].type->element;
                                break;
                            }
                        }
                    }
                    if (source_element == NULL) {
                        fprintf(stderr,
                                "native DAT DynamicsDesc lacks source view\n");
                        abort();
                    }
                    {
                        uint32_t target = raw_pointer(
                            context, source_offset + field->source_offset);
                        void* pointer = NULL;
                        if (target != UINT32_MAX) {
                            pointer = translate_target_count(
                                context, target, source_element, count);
                        }
                        memcpy((uint8_t*) native + field->native_offset,
                               &pointer, sizeof(pointer));
                    }
                } else {
                    translate_value(context, field->type,
                                    source_offset + field->source_offset,
                                    (uint8_t*) native + field->native_offset);
                }
            }
            return;
        }
        for (i = 0; i < type->field_count; ++i) {
            const MslDatField* field = &type->fields[i];
            translate_value(context, field->type,
                            source_offset + field->source_offset,
                            (uint8_t*) native + field->native_offset);
        }
        return;
    case MSL_DAT_UNION:
        if (strcmp(type->name, "CmdUnion") == 0 ||
            strcmp(type->name, "ColorOverlay_x8_t") == 0)
        {
            uint32_t target = raw_pointer(context, source_offset);

            // Action and color scripts are streams of overlapping PPC
            // bitfield views.
            // Preserve each source word's big-endian storage so every command
            // view observes the same bits under the native
            // scalar-storage-order declarations. Relocation words used by
            // Subroutine/Goto instead become native command-stream pointers in
            // the widened union slot.
            // refs/melee/src/melee/lb/{types.h,lbcommand.c}
            if (target != UINT32_MAX) {
                void* pointer = translate_target(context, target, type);
                memcpy(native, &pointer, sizeof(pointer));
            } else {
                memcpy(native, context->data + source_offset,
                       type->source_size);
            }
            return;
        }
        if (type->field_count != 0) {
            const MslDatField* field = &type->fields[0];
            translate_value(context, field->type,
                            source_offset + field->source_offset,
                            (uint8_t*) native + field->native_offset);
        }
        return;
    case MSL_DAT_POINTER: {
        uint32_t target = raw_pointer(context, source_offset);
        void* pointer = NULL;
        if (target != UINT32_MAX) {
            if (type->element->kind == MSL_DAT_FUNCTION) {
                fprintf(stderr,
                        "native DAT contains a non-null function pointer at %x\n",
                        source_offset);
                abort();
            }
            pointer = translate_target(context, target, type->element);
        }
        memcpy(native, &pointer, sizeof(pointer));
        return;
    }
    case MSL_DAT_ARRAY:
    {
        uint32_t count = type->count;
        uint32_t boundary = next_boundary(context, source_offset);
        uint32_t span = boundary - source_offset;

        // Several DAT declarations are fixed-capacity runtime containers whose
        // archive instance stores only the populated prefix (notably
        // ItemStateArray). The next relocation target is the next packed
        // object; leave the native capacity's unused tail zero-initialized.
        if (type->element->source_size != 0 &&
            span / type->element->source_size < count)
        {
            count = span / type->element->source_size;
        }
        for (i = 0; i < count; ++i) {
            translate_value(context, type->element,
                            source_offset + i * type->element->source_size,
                            (uint8_t*) native +
                                (size_t) i * type->element->native_size);
        }
        return;
    }
    case MSL_DAT_FUNCTION:
        return;
    case MSL_DAT_BITFIELD:
        translate_bitfield(type, context->data + source_offset,
                           (uint8_t*) native);
        return;
    }
}

int msl_native_archive_parse(HSD_Archive* archive, uint8_t* source,
                             size_t file_size)
{
    MslNativeArchive* context;
    uint32_t offset;
    uint32_t i;
    uint32_t boundaries_capacity;

    for (i = 0; i < native_archive_cache_count; ++i) {
        MslNativeArchiveCacheEntry* entry = &native_archive_cache[i];
        if (entry->source == source && entry->file_size == file_size) {
            *archive = entry->archive;
            return 0;
        }
    }
    require_dat_initialization("uncached native archive parse");

    if (archive == NULL || source == NULL || file_size < 0x20) {
        fprintf(stderr, "native DAT invalid parse arguments\n");
        return -1;
    }
    memset(archive, 0, sizeof(*archive));
    archive->header.file_size = read_be32(source + 0x00);
    archive->header.data_size = read_be32(source + 0x04);
    archive->header.nb_reloc = read_be32(source + 0x08);
    archive->header.nb_public = read_be32(source + 0x0C);
    archive->header.nb_extern = read_be32(source + 0x10);
    memcpy(archive->header.version, source + 0x14,
           sizeof(archive->header.version));
    if (archive->header.file_size != file_size ||
        archive->header.data_size > file_size - 0x20)
    {
        fprintf(stderr,
                "native DAT invalid header: file=%u/%zu data=%u\n",
                archive->header.file_size, file_size,
                archive->header.data_size);
        return -1;
    }
    archive->flags = HSD_ARCHIVE_DONT_FREE;
    archive->data = source + 0x20;
    offset = 0x20 + archive->header.data_size;

    context = native_alloc(sizeof(*context));
    context->archive = archive;
    context->source = source;
    context->data = archive->data;
    context->data_size = archive->header.data_size;
    context->reloc_count = archive->header.nb_reloc;
    context->reloc_fields = native_alloc(
        (size_t) context->reloc_count * sizeof(*context->reloc_fields));
    boundaries_capacity = context->reloc_count + archive->header.nb_public + 1;
    context->boundaries = native_alloc(
        (size_t) boundaries_capacity * sizeof(*context->boundaries));

    for (i = 0; i < context->reloc_count; ++i) {
        uint32_t field = read_be32(source + offset + i * 4);
        uint32_t target;
        if (field + 4 > context->data_size) {
            fprintf(stderr, "native DAT relocation %u field %x out of body %x\n",
                    i, field, context->data_size);
            return -1;
        }
        context->reloc_fields[i] = field;
        target = read_be32(context->data + field);
        if (target < context->data_size) {
            context->boundaries[context->boundary_count++] = target;
        }
    }
    qsort(context->reloc_fields, context->reloc_count, sizeof(uint32_t),
          compare_u32);
    offset += archive->header.nb_reloc * 4;

    archive->public_info = native_alloc(
        (size_t) archive->header.nb_public * sizeof(*archive->public_info));
    for (i = 0; i < archive->header.nb_public; ++i) {
        archive->public_info[i].offset = read_be32(source + offset + i * 8);
        archive->public_info[i].symbol =
            read_be32(source + offset + i * 8 + 4);
        if (archive->public_info[i].offset < context->data_size) {
            context->boundaries[context->boundary_count++] =
                archive->public_info[i].offset;
        }
    }
    offset += archive->header.nb_public * 8;

    archive->extern_info = native_alloc(
        (size_t) archive->header.nb_extern * sizeof(*archive->extern_info));
    for (i = 0; i < archive->header.nb_extern; ++i) {
        archive->extern_info[i].offset = read_be32(source + offset + i * 8);
        archive->extern_info[i].symbol =
            read_be32(source + offset + i * 8 + 4);
    }
    offset += archive->header.nb_extern * 8;
    if (offset > file_size) {
        fprintf(stderr, "native DAT metadata end %x exceeds file %zx\n", offset,
                file_size);
        return -1;
    }
    archive->symbols = (char*) source + offset;
    context->boundaries[context->boundary_count++] = context->data_size;
    qsort(context->boundaries, context->boundary_count, sizeof(uint32_t),
          compare_u32);
    archive->top_ptr = context;
    if (native_archive_cache_count == MSL_NATIVE_ARCHIVE_CACHE_CAPACITY) {
        fprintf(stderr, "native DAT archive cache exhausted\n");
        abort();
    }
    native_archive_cache[native_archive_cache_count].source = source;
    native_archive_cache[native_archive_cache_count].file_size = file_size;
    native_archive_cache[native_archive_cache_count].archive = *archive;
    native_archive_cache_count += 1;
    return 0;
}

static const MslDatType* public_type(const char* symbol)
{
    size_t length = strlen(symbol);
    if (strcmp(symbol, "map_head") == 0) {
        return msl_dat_root_UnkStageDat;
    }
    if (strcmp(symbol, "coll_data") == 0) {
        return msl_dat_root_MapCollData;
    }
    if (strcmp(symbol, "grGroundParam") == 0) {
        return msl_dat_root_UnkStage6B0;
    }
    if (strcmp(symbol, "quake_model_set") == 0) {
        return msl_dat_root_DynamicModelDesc;
    }
    if (strcmp(symbol, "ftDataFox") == 0 ||
        strcmp(symbol, "ftDataCaptain") == 0 ||
        strcmp(symbol, "ftDataSeak") == 0 ||
        strcmp(symbol, "ftDataLuigi") == 0 ||
        strcmp(symbol, "ftDataSamus") == 0 ||
        strcmp(symbol, "ftDataPopo") == 0 ||
        strcmp(symbol, "ftDataNana") == 0 ||
        strcmp(symbol, "ftDataDonkey") == 0 ||
        strcmp(symbol, "ftDataGanon") == 0 ||
        strcmp(symbol, "ftDataPikachu") == 0 ||
        strcmp(symbol, "ftDataYoshi") == 0 ||
        strcmp(symbol, "ftDataMario") == 0 ||
        strcmp(symbol, "ftDataDrmario") == 0 ||
        strcmp(symbol, "ftDataMars") == 0 ||
        strcmp(symbol, "ftDataPeach") == 0 ||
        strcmp(symbol, "ftDataZelda") == 0 ||
        strcmp(symbol, "ftDataFalco") == 0)
    {
        return msl_dat_root_ftData;
    }
    if (strcmp(symbol, "itPublicData") == 0) {
        return msl_dat_root_it_804D6D20_t;
    }
    if (length >= 9 && strcmp(symbol + length - 9, "_figatree") == 0) {
        return msl_dat_root_FigaTree;
    }
    if (length >= 14 &&
        strcmp(symbol + length - 14, "_matanim_joint") == 0)
    {
        return msl_dat_root_HSD_MatAnimJoint;
    }
    if (length >= 6 && strcmp(symbol + length - 6, "_joint") == 0) {
        return msl_dat_root_HSD_Joint;
    }
    return NULL;
}

static void* translate_stage_params(MslNativeArchive* context,
                                    uint32_t offset)
{
    const MslDatType* type;

    // Both imported stage owners publish the same DAT symbol with different
    // anonymous source structs. The source internal-stage owner selected
    // before grDatFiles_801C6038 is the concrete type authority for this
    // public. refs/melee/src/melee/gr/{grbattle.c,grpstadium.c}
    if (stage_info.internal_stage_id == BATTLE) {
        type = msl_dat_root_MslDatBattlefieldParams;
    } else if (stage_info.internal_stage_id == PSTADIUM) {
        type = msl_dat_root_MslDatPokemonStadiumParams;
    } else if (stage_info.internal_stage_id == IZUMI) {
        type = msl_dat_root_MslDatFountainParams;
    } else if (stage_info.internal_stage_id == STORY) {
        type = msl_dat_root_MslDatYoshisStoryParams;
    } else if (stage_info.internal_stage_id == OLDPUPUPU) {
        type = msl_dat_root_MslDatDreamLandParams;
    } else if (stage_info.internal_stage_id == LAST) {
        // The validated manual FD owner does not consume this presentation
        // parameter, matching the prior untranslated public projection.
        return NULL;
    } else {
        fprintf(stderr,
                "native DAT yakumono_param has unsupported internal stage %d\n",
                stage_info.internal_stage_id);
        abort();
    }
    return translate_target_count(context, offset, type, 1);
}

static void* translate_stage_item_public(MslNativeArchive* context,
                                         uint32_t offset)
{
    enum {
        STAGE_ITEM_KIND_SOURCE_OFFSET = 0x00,
        STAGE_ITEM_ARTICLE_SOURCE_OFFSET = 0x04,
        ARTICLE_SPECIAL_ATTRS_SOURCE_OFFSET = 0x04,
    };
    typedef struct MslNativeStageItemEntry {
        int kind;
        Article* article;
    } MslNativeStageItemEntry;
    uint32_t count = 0;
    uint32_t i;
    MslNativeStageItemEntry** result;

    while (raw_pointer(context, offset + count * 4) != UINT32_MAX) {
        count += 1;
    }
    result = native_alloc((size_t) (count + 1) * sizeof(*result));
    for (i = 0; i < count; ++i) {
        uint32_t entry_source = raw_pointer(context, offset + i * 4);
        uint32_t article_source;
        result[i] = translate_target_count(
            context, entry_source, msl_dat_root_MslDatStageItemEntry, 1);
        article_source = raw_pointer(
            context, entry_source + STAGE_ITEM_ARTICLE_SOURCE_OFFSET);
        if (result[i]->kind == It_Kind_Heiho && article_source != UINT32_MAX) {
            uint32_t attrs_source = raw_pointer(
                context,
                article_source + ARTICLE_SPECIAL_ATTRS_SOURCE_OFFSET);
            if (result[i]->article == NULL || attrs_source == UINT32_MAX) {
                fprintf(stderr,
                        "native GrSt.dat Heiho article is incomplete\n");
                abort();
            }
            result[i]->article->x4_specialAttributes =
                translate_target_count(context, attrs_source,
                                       msl_dat_root_MslDatHeihoAttrs, 1);
        }
        (void) STAGE_ITEM_KIND_SOURCE_OFFSET;
    }
    return result;
}

static void* translate_item_public(MslNativeArchive* context,
                                   uint32_t offset)
{
    enum {
        COMMON_ITEM_COUNT = 43,
        CHARACTER_ITEM_COUNT = 118,
        POKEMON_ITEM_COUNT = 47,
    };
    it_804D6D20_t* result = native_alloc(sizeof(*result));
    uint32_t target;
    uint32_t article_source;
    uint32_t attrs_source;

    add_memo(context, offset, msl_dat_root_it_804D6D20_t, result);
    target = raw_pointer(context, offset + 0x00);
    if (target != UINT32_MAX) {
        result->x0 = translate_target_count(
            context, target, msl_dat_root_ItemCommonData, 1);
    }
    // Translate the source-owned common Article table. Stage item spawning is
    // disabled, but Peach's SpecialLw directly selects BombHei, Dosei, or
    // Sword from this table. Article.x4 is void in the decomp, so install the
    // three concrete source attribute layouts explicitly after translating
    // the shared Article graphs.
    // refs/melee/src/melee/it/iteffect.c::it_802787B4
    // refs/melee/src/melee/ft/chara/ftPeach/ftPe_SpecialLw.c
    target = raw_pointer(context, offset + 0x04);
    if (target != UINT32_MAX) {
        result->x4 = translate_target_count(
            context, target, msl_dat_root_MslDatCommonItemArticles, 1);
#define TRANSLATE_COMMON_ITEM_ATTRS(kind, type)                               \
    do {                                                                      \
        article_source = raw_pointer(context, target + (kind) * 4);           \
        if (article_source != UINT32_MAX && result->x4[(kind)] != NULL) {     \
            attrs_source = raw_pointer(context, article_source + 0x04);       \
            if (attrs_source != UINT32_MAX) {                                 \
                result->x4[(kind)]->x4_specialAttributes =                    \
                    translate_target_count(context, attrs_source, (type), 1); \
            }                                                                 \
        }                                                                     \
    } while (0)
        TRANSLATE_COMMON_ITEM_ATTRS(It_Kind_BombHei,
                                    msl_dat_root_itBombHeiAttributes);
        TRANSLATE_COMMON_ITEM_ATTRS(It_Kind_Dosei,
                                    msl_dat_root_itDoseiAttributes);
        TRANSLATE_COMMON_ITEM_ATTRS(It_Kind_Sword,
                                    msl_dat_root_itSword_UnkArticle1);
#undef TRANSLATE_COMMON_ITEM_ATTRS
    } else {
        result->x4 = native_alloc(COMMON_ITEM_COUNT * sizeof(*result->x4));
    }
    // Character articles are installed later by each fighter's OnLoad path.
    // refs/melee/src/melee/it/item.c::Item_80267978
    result->x8 = native_alloc(CHARACTER_ITEM_COUNT * sizeof(*result->x8));
    result->xC = native_alloc(POKEMON_ITEM_COUNT * sizeof(*result->xC));
    target = raw_pointer(context, offset + 0x10);
    if (target != UINT32_MAX) {
        result->x10 = translate_target_count(
            context, target, msl_dat_root_it_804D6D40_t, 1);
    }
    target = raw_pointer(context, offset + 0x14);
    if (target != UINT32_MAX) {
        result->x14 = translate_target(
            context, target, msl_dat_root_Fighter_804D653C_t);
    }
    return result;
}

// PlCo.dat's CPU input block. The DWARF layout carries its members as void
// pointers, so the generic translation copies the payloads as raw big-endian
// bytes and the CPU attack evaluator reads byte-swapped garbage: no
// CPU-driven fighter ever selects an attack. Translate the consumed shapes
// by hand: per-kind pointer arrays of cmd-terminated 0x24-byte attack
// entries (uniform 32-bit lanes), the per-kind distance floats, the weapon
// reach floats, and the byte-encoded command scripts.
// refs/melee/src/melee/ft/ftcpuattack.c::{ftCo_800B4AB0,ftCo_800B8A9C}
// refs/melee/src/melee/ft/ftcmdscript.c::{ftCo_800B3E04,ftCo_800B4880}
enum {
    MSL_CPU_TABLE_KINDS = 33,       // tables index by fp->kind
    // Scripts index by CPU attack/defend command ids (0x28/0x29-class rows
    // land here beside the literal 38/0x26 call sites); slots past the
    // source array read as non-relocated fields and stay NULL.
    MSL_CPU_CMDSCRIPT_COUNT = 64,
    MSL_CPU_WEAPON_REACH_COUNT = 6, // Harisen..Parasol reach bonuses
    MSL_CPU_ATTACK_ENTRY_BYTES = 0x24,
};

static void* translate_cpu_attack_entries(MslNativeArchive* context,
                                          uint32_t offset)
{
    uint32_t count = 0;
    uint32_t words;
    uint32_t w;
    uint32_t* native;

    while (offset + (count + 1) * MSL_CPU_ATTACK_ENTRY_BYTES <=
               context->data_size &&
           read_be32(context->data + offset +
                     count * MSL_CPU_ATTACK_ENTRY_BYTES) != 0)
    {
        ++count;
    }
    words = count * (MSL_CPU_ATTACK_ENTRY_BYTES / 4);
    native = native_alloc((count + 1) * MSL_CPU_ATTACK_ENTRY_BYTES);
    for (w = 0; w < words; ++w) {
        native[w] = read_be32(context->data + offset + w * 4);
    }
    for (w = words; w < (count + 1) * (MSL_CPU_ATTACK_ENTRY_BYTES / 4); ++w) {
        native[w] = 0;
    }
    return native;
}

static void* translate_cpu_attack_table(MslNativeArchive* context,
                                        uint32_t offset)
{
    void** native = native_alloc(MSL_CPU_TABLE_KINDS * sizeof(*native));
    uint32_t kind;

    for (kind = 0; kind < MSL_CPU_TABLE_KINDS; ++kind) {
        uint32_t target = raw_pointer(context, offset + kind * 4);
        native[kind] = target != UINT32_MAX
                           ? translate_cpu_attack_entries(context, target)
                           : NULL;
    }
    return native;
}

static void* translate_cpu_float_array(MslNativeArchive* context,
                                       uint32_t offset, uint32_t count)
{
    uint32_t* native = native_alloc(count * sizeof(*native));
    uint32_t i;

    for (i = 0; i < count; ++i) {
        native[i] = read_be32(context->data + offset + i * 4);
    }
    return native;
}

static void* translate_cpu_cmdscript(MslNativeArchive* context,
                                     uint32_t offset)
{
    // Byte-encoded command stream: opcodes above 0xBF carry two argument
    // bytes, above 0x7F one, and 0x7F terminates.
    // refs/melee/src/melee/ft/ftcmdscript.c::ftCo_800B4880
    uint32_t end = offset;
    uint8_t* native;
    uint32_t size;

    while (end < context->data_size && context->data[end] != 0x7F) {
        uint8_t cmd = context->data[end];
        end += 1 + (cmd > 0xBF ? 2 : cmd > 0x7F ? 1 : 0);
    }
    size = end + 1 - offset;
    native = native_alloc(size);
    memcpy(native, context->data + offset, size);
    return native;
}

static void* translate_fighter_cpu_tables(MslNativeArchive* context,
                                          uint32_t offset)
{
    void** result = native_alloc(10 * sizeof(*result));
    uint32_t member;

    for (member = 0; member < 10; ++member) {
        uint32_t target = raw_pointer(context, offset + member * 4);
        if (target == UINT32_MAX) {
            result[member] = NULL;
            continue;
        }
        switch (member) {
        case 0: { // cmdscripts: script pointer array
            void** scripts =
                native_alloc(MSL_CPU_CMDSCRIPT_COUNT * sizeof(*scripts));
            uint32_t i;
            for (i = 0; i < MSL_CPU_CMDSCRIPT_COUNT; ++i) {
                uint32_t script = raw_pointer(context, target + i * 4);
                scripts[i] = script != UINT32_MAX
                                 ? translate_cpu_cmdscript(context, script)
                                 : NULL;
            }
            result[member] = scripts;
            break;
        }
        case 8: // x20: per-kind distance thresholds
            result[member] = translate_cpu_float_array(context, target,
                                                       MSL_CPU_TABLE_KINDS);
            break;
        case 9: // x24: held-weapon reach bonuses
            result[member] = translate_cpu_float_array(
                context, target, MSL_CPU_WEAPON_REACH_COUNT);
            break;
        default: // x4..x1C: per-kind attack entry tables
            result[member] = translate_cpu_attack_table(context, target);
            break;
        }
    }
    return result;
}

static void* translate_fighter_common_public(MslNativeArchive* context,
                                             uint32_t offset)
{
    const MslDatType* const element_types[23] = {
        msl_dat_root_ftCommonData,
        // PlCo.dat's second public pointer is the 26-entry throw-attribute
        // table consumed across ftCo_MS_LightThrowF..HeavyThrowLw4.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ItemThrow.c::{
        //   ftCo_80095D5C,ftCo_80095EFC}
        msl_dat_root_MslDatItemThrowAttrs,
        msl_dat_root_MslDatFloat5,
        msl_dat_root_MslDatFloat,
        msl_dat_root_MslDatFighterPartsPointer,
        msl_dat_root_MslDatFighter6540Pointer,
        msl_dat_root_Fighter_804D653C_t,
        msl_dat_root_Fighter_804D653C_t,
        // refs/melee/src/melee/ft/ft_0D4D.c::ftCo_800D4FF4
        msl_dat_root_Fighter_804D6534_t,
        msl_dat_root_MslDatVec2Pointer,
        msl_dat_root_MslDatByte,
        msl_dat_root_Fighter_804D6528_t,
        msl_dat_root_Fighter_804D6524_t,
        msl_dat_root_Fighter_804D6520_t,
        msl_dat_root_Fighter_804D651C_t,
        msl_dat_root_Fighter_804D6518_t,
        msl_dat_root_HSD_Joint,
        msl_dat_root_MslDatByte,
        msl_dat_root_MslDatByte,
        msl_dat_root_MslDatByte,
        msl_dat_root_HSD_Joint,
        msl_dat_root_CrowdConfig,
        msl_dat_root_Fighter_804D64FC_t,
    };
    void** result = native_alloc(sizeof(*result) * 23);
    uint32_t i;

    for (i = 0; i < 23; ++i) {
        uint32_t target = raw_pointer(context, offset + i * 4);
        if (target != UINT32_MAX) {
            result[i] = i == 22
                            ? translate_fighter_cpu_tables(context, target)
                            : translate_target(context, target,
                                               element_types[i]);
        }
    }
    return result;
}

typedef enum MslFighterArticleProfile {
    MSL_FIGHTER_ARTICLES_NONE,
    MSL_FIGHTER_ARTICLES_FOX,
    MSL_FIGHTER_ARTICLES_FALCO,
    MSL_FIGHTER_ARTICLES_SHEIK,
    MSL_FIGHTER_ARTICLES_PEACH,
    MSL_FIGHTER_ARTICLES_ZELDA,
    MSL_FIGHTER_ARTICLES_LUIGI,
    MSL_FIGHTER_ARTICLES_MARIO,
    MSL_FIGHTER_ARTICLES_MARIOD,
    MSL_FIGHTER_ARTICLES_SAMUS,
    MSL_FIGHTER_ARTICLES_ICECLIMBER,
    MSL_FIGHTER_ARTICLES_PIKACHU,
    MSL_FIGHTER_ARTICLES_YOSHI,
    MSL_FIGHTER_AUX_PURIN_PARTS,
} MslFighterArticleProfile;

static ftData* translate_fighter_public(
    MslNativeArchive* context, uint32_t offset,
    const MslDatType* attrs_type, uint32_t anim_count,
    MslFighterArticleProfile article_profile)
{
    enum {
        FT_DATA_X48_ITEMS_SOURCE_OFFSET = 0x48,
        ARTICLE_SPECIAL_ATTRS_SOURCE_OFFSET = 0x04,
        MAX_REACHED_ARTICLE_COUNT = 5,
    };
    const MslDatType* article_list_type = NULL;
    const MslDatType* attr_types[MAX_REACHED_ARTICLE_COUNT] = { NULL };
    uint8_t indices[MAX_REACHED_ARTICLE_COUNT] = { 0 };
    uint32_t article_count = 0;
    ftData* result = translate_target_count(
        context, offset, msl_dat_root_ftData, 1);
    uint32_t target = raw_pointer(context, offset + 0x04);
    uint32_t list = raw_pointer(
        context, offset + FT_DATA_X48_ITEMS_SOURCE_OFFSET);
    uint32_t i;

    if (target == UINT32_MAX) {
        fprintf(stderr, "native fighter DAT is missing special attributes\n");
        abort();
    }
    result->ext_attr =
        translate_target_count(context, target, attrs_type, 1);
    target = raw_pointer(context, offset + 0x0C);
    if (target == UINT32_MAX) {
        fprintf(stderr, "native fighter DAT is missing its animation table\n");
        abort();
    }
    result->xC = translate_target_count(
        context, target, msl_dat_root_Fighter_WaitAnimData, anim_count);
    target = raw_pointer(context, offset + 0x10);
    if (target == UINT32_MAX) {
        fprintf(stderr, "native fighter DAT is missing animation metadata\n");
        abort();
    }
    result->x10 = translate_target_count(
        context, target, msl_dat_root_MslDatAnimBytePair, anim_count);

    switch (article_profile) {
    case MSL_FIGHTER_ARTICLES_NONE:
        break;
    case MSL_FIGHTER_ARTICLES_FOX:
    case MSL_FIGHTER_ARTICLES_FALCO:
        article_list_type = msl_dat_root_MslDatSpaceAnimalArticles;
        article_count = 3;
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = article_profile == MSL_FIGHTER_ARTICLES_FALCO ? 3 : 2;
        attr_types[0] = msl_dat_root_FoxLaserAttr;
        attr_types[1] = msl_dat_root_FoxBlasterAttr;
        attr_types[2] = msl_dat_root_FoxIllusionAttr;
        break;
    case MSL_FIGHTER_ARTICLES_SHEIK:
        article_list_type = msl_dat_root_MslDatSheikArticles;
        article_count = 4;
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        indices[3] = 3;
        attr_types[0] = msl_dat_root_itSeakNeedleThrownAttributes;
        attr_types[3] = msl_dat_root_itSeakChain_Attrs;
        break;
    case MSL_FIGHTER_ARTICLES_PEACH:
        article_list_type = msl_dat_root_MslDatPeachArticles;
        article_count = 5;
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        indices[3] = 3;
        indices[4] = 4;
        attr_types[1] = msl_dat_root_MslDatPeachTurnipAttrs;
        attr_types[4] = msl_dat_root_itPeachToadSporeAttributes;
        break;
    case MSL_FIGHTER_ARTICLES_ZELDA:
        article_list_type = msl_dat_root_MslDatZeldaArticles;
        article_count = 2;
        indices[0] = 0;
        indices[1] = 1;
        attr_types[0] = msl_dat_root_MslDatZeldaDinFireAttrs;
        attr_types[1] = msl_dat_root_itZeldaDinFireExplodeAttributes;
        break;
    case MSL_FIGHTER_ARTICLES_LUIGI:
        article_list_type = msl_dat_root_MslDatLuigiArticles;
        article_count = 1;
        indices[0] = 0;
        attr_types[0] = msl_dat_root_itUnkAttributes;
        break;
    case MSL_FIGHTER_ARTICLES_MARIO:
        // PlMr/PlDr share one four-slot article layout, but each DAT only
        // populates its own owner's slots: Mario registers items[0]
        // (fireball) and items[2] (cape); Dr. Mario registers items[1]
        // (megavitamin) and items[3] (super sheet). The fireball and
        // megavitamin attribute blocks share the five-float itUnkAttributes
        // shape; the capes declare no special attributes.
        // refs/melee/src/melee/ft/chara/{ftMario/ftMr_Init.c,
        // ftDrMario/ftDr_Init.c}
        article_list_type = msl_dat_root_MslDatMarioArticles;
        article_count = 2;
        indices[0] = 0;
        indices[1] = 2;
        attr_types[0] = msl_dat_root_itUnkAttributes;
        break;
    case MSL_FIGHTER_ARTICLES_MARIOD:
        article_list_type = msl_dat_root_MslDatMarioArticles;
        article_count = 2;
        indices[0] = 1;
        indices[1] = 3;
        attr_types[0] = msl_dat_root_itUnkAttributes;
        break;
    case MSL_FIGHTER_ARTICLES_SAMUS:
        // PlSs.dat's five-slot list holds the four articles registered by
        // ftSs_Init_OnLoad (bomb, charge shot, missile, grapple beam), each
        // with its concrete attribute block, and the throw grapple-beam
        // accessory graph the list type translates structurally.
        // refs/melee/src/melee/ft/chara/ftSamus/ftSs_Init.c
        // refs/melee/src/melee/it/items/{itsamusbomb.c,itsamuschargeshot.c,
        //   itsamusmissile.c,itsamusgrapple.c}
        article_list_type = msl_dat_root_MslDatSamusArticles;
        article_count = 4;
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        indices[3] = 3;
        attr_types[0] = msl_dat_root_itSamusBombAttributes;
        attr_types[1] = msl_dat_root_itSamusChargeShot_Attributes;
        attr_types[2] = msl_dat_root_itSamusMissileAttributes;
        attr_types[3] = msl_dat_root_itSamusGrappleAttributes;
        break;
    case MSL_FIGHTER_ARTICLES_ICECLIMBER:
        // PlPp.dat and PlNn.dat share one three-slot article layout: the ice
        // shot, blizzard, and belay-string articles ftPp_Init_OnLoad registers
        // as item kinds 106/107/113. Nana's DAT carries its own copies of the
        // same graphs; her OnLoad registers nothing, so both files translate
        // through the one list type.
        // refs/melee/src/melee/ft/chara/ftPopo/ftPp_Init.c
        // refs/melee/src/melee/it/items/{itclimbersice.c,itclimbersblizzard.c,
        //   itclimbersstring.c}
        article_list_type = msl_dat_root_MslDatIceClimberArticles;
        article_count = 3;
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        attr_types[0] = msl_dat_root_itClimbersIceAttributes;
        attr_types[1] = msl_dat_root_itClimbersBlizzardAttributes;
        attr_types[2] = msl_dat_root_itClimbersStringAttributes;
        break;
    case MSL_FIGHTER_ARTICLES_PIKACHU:
        // PlPk.dat's x48_items leads with the three articles
        // ftPk_Init_OnLoad registers under the item kinds stored in its
        // attribute block (xDC thunder 81, x14 ground jolt 89, x18 air jolt
        // 90). The thunder and ground jolt carry concrete attribute blocks;
        // the air jolt's ported logic never reads x4_specialAttributes. The
        // trailing DAT slots hold presentation graphs no ported code
        // reaches.
        // refs/melee/src/melee/ft/chara/ftPikachu/ftPk_Init.c
        // refs/melee/src/melee/it/items/{itpikachuthunder.c,
        //   itpikachutjoltground.c,itpikachutjoltair.c}
        article_list_type = msl_dat_root_MslDatPikachuArticles;
        article_count = 3;
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        attr_types[0] = msl_dat_root_itPikachuthunderAttributes;
        attr_types[1] = msl_dat_root_itPikachutJoltGroundAttributes;
        break;
    case MSL_FIGHTER_ARTICLES_YOSHI:
        // PlYs.dat's x48_items leads with the three articles
        // ftYs_Init_OnLoad registers as item kinds 86 (thrown egg), 88
        // (Yoshi Bomb star), and 87 (egg-lay capture egg). The thrown egg
        // and star both carry two-float attribute blocks (ityoshistar.c's
        // file-local StarAttrs shares the itYoshiEggThrowAttributes shape);
        // the egg-lay article's attribute block is never read by ported
        // code -- its spawn attributes are built from the fighter's
        // ext-attrs in ftYs_SpecialN.
        // refs/melee/src/melee/ft/chara/ftYoshi/ftYs_Init.c
        // refs/melee/src/melee/it/items/{ityoshieggthrow.c,ityoshistar.c,
        //   ityoshiegglay.c}
        article_list_type = msl_dat_root_MslDatYoshiArticles;
        article_count = 3;
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        attr_types[0] = msl_dat_root_itYoshiEggThrowAttributes;
        attr_types[1] = msl_dat_root_itYoshiEggThrowAttributes;
        break;
    case MSL_FIGHTER_AUX_PURIN_PARTS:
        // x48_items is not an article table for Purin. Its second pointer owns
        // the costume FtPartsDesc consumed by ftPr_Init_8013C360.
        // refs/melee/src/melee/ft/chara/ftPurin/ftPr_Init.c
        article_list_type = msl_dat_root_MslDatPurinAuxList;
        break;
    }

    if (article_profile == MSL_FIGHTER_ARTICLES_NONE) {
        if (list != UINT32_MAX) {
            fprintf(stderr,
                    "native fighter DAT has an untyped character article list\n");
            abort();
        }
        return result;
    }
    if (list == UINT32_MAX) {
        fprintf(stderr, "native fighter DAT is missing its article list\n");
        abort();
    }
    result->x48_items = translate_target(context, list, article_list_type);

    // Article.x4 is void in the decomp. The character OnLoad registrations and
    // reached item implementations are its concrete source type authority.
    // refs/melee/src/melee/ft/chara/{ftFox/ftFx_Init.c,
    //   ftFalco/ftFc_Init.c,ftSeak/ftSk_Init.c,ftPeach/ftPe_Init.c,
    //   ftZelda/ftZd_Init.c,ftLuigi/ftLg_Init.c}
    // refs/melee/src/melee/it/items/{itfoxlaser.c,itfoxblaster.c,
    //   itfoxillusion.c,itseakneedlethrown.c,itseakchain.c,
    //   itpeachturnip.c,itpeachtoadspore.c,itzeldadinfire.c,
    //   itzeldadinfireexplode.c,itluigifireball.c}
    if (result->x48_items == NULL) {
        fprintf(stderr, "native fighter DAT is missing its article list\n");
        abort();
    }
    for (i = 0; i < article_count; ++i) {
        uint32_t index = indices[i];
        uint32_t article = raw_pointer(context, list + index * 4);
        uint32_t attrs;
        if (article == UINT32_MAX || result->x48_items[index] == NULL) {
            fprintf(stderr, "native fighter DAT article %u is incomplete\n",
                    index);
            abort();
        }
        if (attr_types[i] != NULL) {
            attrs = raw_pointer(
                context, article + ARTICLE_SPECIAL_ATTRS_SOURCE_OFFSET);
            if (attrs == UINT32_MAX) {
                fprintf(stderr,
                        "native fighter DAT article %u lacks attributes\n",
                        index);
                abort();
            }
            ((Article*) result->x48_items[index])->x4_specialAttributes =
                translate_target_count(context, attrs, attr_types[i], 1);
        }
    }
    return result;
}

static MslNativePublic* find_public(MslNativeArchive* context,
                                    const char* symbol)
{
    uint32_t i;
    for (i = 0; i < context->public_count; ++i) {
        if (strcmp(context->publics[i].symbol, symbol) == 0) {
            return &context->publics[i];
        }
    }
    return NULL;
}

static void cache_public(MslNativeArchive* context, const char* symbol,
                         void* address)
{
    size_t length;
    char* symbol_copy;

    if (context->public_count == context->public_capacity) {
        uint32_t capacity = context->public_capacity == 0
                                ? 16
                                : context->public_capacity * 2;
        MslNativePublic* replacement =
            native_alloc((size_t) capacity * sizeof(*replacement));
        if (context->publics != NULL) {
            memcpy(replacement, context->publics,
                   (size_t) context->public_count * sizeof(*replacement));
        }
        context->publics = replacement;
        context->public_capacity = capacity;
    }
    length = strlen(symbol) + 1;
    symbol_copy = native_alloc(length);
    memcpy(symbol_copy, symbol, length);
    context->publics[context->public_count++] =
        (MslNativePublic) { symbol_copy, address };
}

void* msl_native_archive_get_public(HSD_Archive* archive, const char* symbol)
{
    MslNativeArchive* context = archive_context(archive);
    MslNativePublic* cached = find_public(context, symbol);
    const MslDatType* type;
    uint32_t offset;
    void* result = NULL;

    if (cached != NULL) {
        return cached->address;
    }
    require_dat_initialization("uncached native archive public lookup");
    type = public_type(symbol);
    offset = raw_public_offset(archive, symbol);
    if (offset == UINT32_MAX) {
        cache_public(context, symbol, NULL);
        return NULL;
    } else if (strcmp(symbol, "plLoadCommonData") == 0) {
        void** table = native_alloc(sizeof(*table));
        uint32_t target = raw_pointer(context, offset);
        if (target != UINT32_MAX) {
            table[0] = translate_target_count(
                context, target, msl_dat_root_pl_804D6470_t, 1);
            result = table;
        }
    } else if (strcmp(symbol, "itPublicData") == 0) {
        result = translate_item_public(context, offset);
    } else if (strcmp(symbol, "ftLoadCommonData") == 0) {
        result = translate_fighter_common_public(context, offset);
    } else if (strcmp(symbol, "ftDataFox") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftFox_DatAttrs, 327,
                                          MSL_FIGHTER_ARTICLES_FOX);
    } else if (strcmp(symbol, "ftDataCaptain") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftCaptain_DatAttrs, 318,
                                          MSL_FIGHTER_ARTICLES_NONE);
    } else if (strcmp(symbol, "ftDataSeak") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftSeakAttributes, 317,
                                          MSL_FIGHTER_ARTICLES_SHEIK);
    } else if (strcmp(symbol, "ftDataSamus") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftSs_DatAttrs, 313,
                                          MSL_FIGHTER_ARTICLES_SAMUS);
    } else if (strcmp(symbol, "ftDataPopo") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftIceClimberAttributes,
                                          321, MSL_FIGHTER_ARTICLES_ICECLIMBER);
    } else if (strcmp(symbol, "ftDataNana") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftIceClimberAttributes,
                                          321, MSL_FIGHTER_ARTICLES_ICECLIMBER);
    } else if (strcmp(symbol, "ftDataDonkey") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftDonkeyAttributes,
                                          337, MSL_FIGHTER_ARTICLES_NONE);
    } else if (strcmp(symbol, "ftDataGanon") == 0) {
        // Ganondorf's ext-attr blob is Captain-shaped: ftGn_Init routes
        // OnLoad/LoadSpecialAttrs through ftCa_Init_OnLoadForGanon and
        // ftCa_Init_LoadSpecialAttrs, so translate with the Captain layout.
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftCaptain_DatAttrs,
                                          318, MSL_FIGHTER_ARTICLES_NONE);
    } else if (strcmp(symbol, "ftDataPikachu") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftPikachuAttributes,
                                          320, MSL_FIGHTER_ARTICLES_PIKACHU);
    } else if (strcmp(symbol, "ftDataYoshi") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftYoshiAttributes,
                                          314, MSL_FIGHTER_ARTICLES_YOSHI);
    } else if (strcmp(symbol, "ftDataLuigi") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftLuigiAttributes, 312,
                                          MSL_FIGHTER_ARTICLES_LUIGI);
    } else if (strcmp(symbol, "ftDataMario") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftMario_DatAttrs, 303,
                                          MSL_FIGHTER_ARTICLES_MARIO);
    } else if (strcmp(symbol, "ftDataDrmario") == 0) {
        // Doc's ext-attr blob is Mario-shaped: ftDrMarioAttributes is a
        // partial alias of ftMario_DatAttrs (x4/xC/x14 overlay specials),
        // and the shared ftMr_* specials read cape_reflection past the
        // 0x18-byte stub, so translate with the full Mario layout.
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftMario_DatAttrs, 303,
                                          MSL_FIGHTER_ARTICLES_MARIOD);
    } else if (strcmp(symbol, "ftDataMars") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_MarsAttributes, 327,
                                          MSL_FIGHTER_ARTICLES_NONE);
    } else if (strcmp(symbol, "ftDataPeach") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftPe_DatAttrs, 318,
                                          MSL_FIGHTER_ARTICLES_PEACH);
    } else if (strcmp(symbol, "ftDataPurin") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftPurinAttributes, 327,
                                          MSL_FIGHTER_AUX_PURIN_PARTS);
    } else if (strcmp(symbol, "ftDataZelda") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftZelda_DatAttrs, 311,
                                          MSL_FIGHTER_ARTICLES_ZELDA);
    } else if (strcmp(symbol, "ftDataFalco") == 0) {
        result = translate_fighter_public(context, offset,
                                          msl_dat_root_ftFox_DatAttrs, 327,
                                          MSL_FIGHTER_ARTICLES_FALCO);
    } else if (strcmp(symbol, "yakumono_param") == 0) {
        result = translate_stage_params(context, offset);
    } else if (strcmp(symbol, "itemdata") == 0) {
        result = translate_stage_item_public(context, offset);
    } else if (type != NULL) {
        result = translate_target_count(context, offset, type, 1);
    }
    cache_public(context, symbol, result);
    return result;
}

char* msl_native_archive_get_extern(HSD_Archive* archive, int index)
{
    if (index < 0 || (uint32_t) index >= archive->header.nb_extern) {
        return NULL;
    }
    return archive->symbols + archive->extern_info[index].symbol;
}

void msl_native_archive_locate_extern(HSD_Archive* archive,
                                      const char* symbol, void* address)
{
    // Reached game archives resolve their externs to null during the source
    // loader. Generic graph translation treats non-relocation pointer fields
    // as null, which is the same result. A non-null external graph owner must
    // gain an explicit translation description before it is admitted.
    (void) archive;
    (void) symbol;
    if (address != NULL) {
        fprintf(stderr, "native DAT non-null external symbols are unsupported\n");
        abort();
    }
}

int msl_native_effect_bank(HSD_Archive* archive, const char* symbol,
                           int* count, HSD_PSCmdList*** commands)
{
    require_dat_initialization("native effect bank translation");
    MslNativeArchive* context = archive_context(archive);
    uint32_t table = raw_public_offset(archive, symbol);
    uint32_t bank;
    uint32_t entries;
    uint32_t first_count;
    uint32_t total;
    uint16_t version;
    HSD_PSCmdList** result;
    uint32_t i;

    if (table == UINT32_MAX) {
        return -1;
    }
    if ((bank = raw_pointer(context, table)) == UINT32_MAX) {
        // A model-only effect archive carries no particle banks: both
        // leading table words are unrelocated NULLs and retail skips
        // psInitDataBank entirely (EfDkData.dat is the supported-domain
        // case). Publish an empty command bank so generator lookups
        // consume nothing, exactly like retail's absent bank.
        // refs/melee/src/melee/ef/efasync.c::efAsync_LoadSync
        if (table + 8 <= context->data_size &&
            read_be32(context->data + table) == 0 &&
            read_be32(context->data + table + 4) == 0 &&
            raw_pointer(context, table + 4) == UINT32_MAX)
        {
            *count = 0;
            *commands = NULL;
            return 0;
        }
        return -1;
    }
    version = read_be16(context->data + bank);
    first_count = read_be32(context->data + bank + 4);
    if (version == 0) {
        total = first_count;
        entries = bank + 8;
    } else if (version >= 0x40 && version <= 0x43) {
        total = first_count + read_be32(context->data + bank + 8);
        entries = bank + 12;
    } else {
        return -1;
    }
    if (total > 65536 ||
        entries + (version == 0 ? total : total - first_count) * 4 >
            context->data_size)
    {
        return -1;
    }
    result = native_alloc((size_t) total * sizeof(*result));
    for (i = version == 0 ? 0 : first_count; i < total; ++i) {
        uint32_t relative = read_be32(
            context->data + entries +
            (version == 0 ? i : i - first_count) * 4);
        if (relative != 0) {
            result[i] = translate_target_count(
                context, bank + relative, msl_dat_root_HSD_PSCmdList, 1);
        }
    }
    *count = (int) total;
    *commands = result;
    return 0;
}

EF_EffectDesc* msl_native_effect_models(HSD_Archive* archive,
                                        const char* symbol, int count)
{
    MslNativeArchive* context;
    uint32_t table;

    require_dat_initialization("native effect model translation");
    if (archive == NULL || symbol == NULL || count <= 0) {
        return NULL;
    }
    context = archive_context(archive);
    table = raw_public_offset(archive, symbol);
    if (table == UINT32_MAX) {
        return NULL;
    }
    // The public effect table stores particle-bank pointers in its first two
    // words, followed immediately by the EF_EffectDesc array used by
    // efLib_Create. Translate the reached prefix before the DAT arena seals.
    // refs/melee/src/melee/ef/{efasync.c::efAsync_LoadSync,
    //     eflib.c::efLib_Create}
    return translate_target_count(context, table + 8,
                                  msl_dat_root_EF_EffectDesc,
                                  (uint32_t) count);
}
