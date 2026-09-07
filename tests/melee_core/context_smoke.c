#include "runtime/scalar.h"
#include "runtime/observation.h"
#include "ft/types.h"
#include "it/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
// Mach-O has no __data_start/_end linker symbols; the writable globals of
// the executable live in its __DATA segment (data, bss, common).
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>

static uint8_t* writable_data_start(void)
{
    unsigned long size;
    return getsegmentdata(&_mh_execute_header, "__DATA", &size);
}

static size_t writable_data_size(void)
{
    unsigned long size;
    (void) getsegmentdata(&_mh_execute_header, "__DATA", &size);
    return (size_t) size;
}
#else
extern uint8_t __data_start[];
extern uint8_t _end[];

static uint8_t* writable_data_start(void)
{
    return __data_start;
}

static size_t writable_data_size(void)
{
    return (size_t) (_end - __data_start);
}
#endif

static uint64_t hash_bytes(const void* data, size_t size)
{
    const uint8_t* bytes = data;
    uint64_t hash = UINT64_C(1469598103934665603);
    while (size-- != 0) {
        hash = (hash ^ *bytes++) * UINT64_C(1099511628211);
    }
    return hash;
}

static int writable_globals_unchanged(const uint8_t* snapshot, size_t size,
                                      const char* phase)
{
    size_t byte;
    if (memcmp(snapshot, writable_data_start(), size) == 0) {
        return 1;
    }
    for (byte = 0; byte < size && snapshot[byte] == writable_data_start()[byte]; ++byte)
    {
    }
    fprintf(stderr,
            "%s mutated process-global state: address=%p byte=%zu:%02x/%02x\n",
            phase, writable_data_start() + byte, byte, snapshot[byte],
            writable_data_start()[byte]);
    return 0;
}

static int in_range(uintptr_t value, const void* base, size_t size)
{
    uintptr_t begin = (uintptr_t) base;
    return base != NULL && value >= begin && value - begin < size;
}

static int raw_objects_have_no_managed_pointers(const MslCoreMatch* match,
                                                const char* phase)
{
    uint32_t object_index;
    const MslRelocRecord* records = msl_reloc_records(match);
    for (object_index = 0; object_index < match->relocation_count;
         ++object_index)
    {
        const MslRelocRecord* object = &records[object_index];
        const void* object_address =
            msl_reloc_record_address(match, object);
        size_t offset;
        if (object->type != MSL_RELOC_RAW || object_address == NULL) {
            continue;
        }
        if ((object->flags & MSL_RELOC_INTRUSIVE_FIRST_POINTER) != 0) {
            uintptr_t first;
            memcpy(&first, object_address, sizeof(first));
            // A raw intrusive object whose first word is NULL or another
            // Match allocation is on a source free list. Its remaining bytes
            // are intentionally stale and not part of live pointer state.
            if (first == 0 ||
                in_range(first, match->memory.arena, match->memory.used))
            {
                continue;
            }
        }
        for (offset = 0; offset + sizeof(uintptr_t) <= object->stride;
             offset += sizeof(uintptr_t))
        {
            uintptr_t value;
            if (offset == 0 &&
                (object->flags & MSL_RELOC_INTRUSIVE_FIRST_POINTER) != 0)
            {
                continue;
            }
            memcpy(&value, (const uint8_t*) object_address + offset,
                   sizeof(value));
            // Managed C objects are at least word-aligned. Requiring that
            // avoids treating a scalar f32 followed by zero padding as a
            // widened host pointer merely because MAP_32BIT placed an arena
            // in the same numeric range.
            if ((value & 3U) == 0 &&
                (in_range(value, match, sizeof(*match)) ||
                 in_range(value, match->memory.arena, match->memory.used) ||
                 in_range(value, match->game_data,
                          sizeof(*match->game_data)) ||
                 in_range(value, match->game_data->memory.arena,
                          match->game_data->memory.used)
#ifdef MSL_CORE_NATIVE
                 || in_range(value, match->game_data->native_dat.arena,
                             match->game_data->native_dat.arena_used)
#endif
                     ))
            {
                fprintf(stderr,
                        "%s found unmanaged pointer in raw relocation: "
                        "object=%u address=%p offset=%zu value=%p stride=%u "
                        "flags=%u\n",
                        phase, object_index, object_address, offset,
                        (void*) value, object->stride, object->flags);
                if ((object->flags & MSL_RELOC_INTRUSIVE_FIRST_POINTER) != 0) {
                    HSD_ClassInfo* info;
                    memcpy(&info, object_address, sizeof(info));
                    if (in_range((uintptr_t) info, writable_data_start(),
                                 writable_data_size()))
                    {
                        fprintf(stderr, "  class=%s size=%d\n",
                                info->head.class_name != NULL
                                    ? info->head.class_name
                                    : "(unnamed)",
                                info->head.obj_size);
                    }
                }
                return 0;
            }
        }
    }
    return 1;
}

static void config_init(MslCoreMatchConfig* config, uint8_t stage_id,
                        uint8_t char_id, uint8_t stock_count)
{
    memset(config, 0, sizeof(*config));
    config->stage_id = stage_id;
    config->frame_id = -123;
    config->frame_pre_random_seed = 1;
    config->initial_random_seed = 1;
    config->match_damage_ratio = 1.0F;
    config->num_players = 2;
    config->stock_count = stock_count;
    config->players[0].char_id = char_id;
    config->players[1].char_id = char_id;
}

static int observation_projection_matches(const MslCoreMatch* match,
                                          const char* phase)
{
    MslCoreObservation direct;
    MslCoreObservation reference;
    uint8_t viewpoint;
    for (viewpoint = 0; viewpoint < match->config.num_players; ++viewpoint) {
        size_t byte;
        if (msl_core_match_write_observation(match, viewpoint, &direct) != 0 ||
            msl_core_match_write_observation_from_compare(
                match, viewpoint, &reference) != 0)
        {
            return 0;
        }
        if (memcmp(&direct, &reference, sizeof(direct)) == 0) {
            continue;
        }
        for (byte = 0; byte < sizeof(direct) &&
                       ((const uint8_t*) &direct)[byte] ==
                           ((const uint8_t*) &reference)[byte];
             ++byte)
        {
        }
        fprintf(stderr,
                "%s observation projection differs: viewpoint=%u "
                "byte=%zu direct=%02x reference=%02x\n",
                phase, viewpoint, byte, ((const uint8_t*) &direct)[byte],
                ((const uint8_t*) &reference)[byte]);
        return 0;
    }
    return 1;
}

/* Nested color loops overlay pointer/count slots beyond CommandInfo's
 * declared three entries; every owning object must relocate all six. */
static int color_return_slots_registered(void)
{
    const MslRelocType types[] = { MSL_RELOC_FIGHTER, MSL_RELOC_FIGHTER,
                                  MSL_RELOC_FIGHTER, MSL_RELOC_ITEM };
    const size_t bases[] = { offsetof(Fighter, x408), offsetof(Fighter, x488),
                             offsetof(Fighter, x508),
                             offsetof(Item, x548_colorOverlay) };
    size_t owner, slot;
    for (owner = 0; owner < 4; ++owner) {
        const MslRelocTypeDesc* desc = &msl_reloc_type_descs[types[owner]];
        for (slot = 0; slot < 6; ++slot) {
            size_t offset = bases[owner] + offsetof(ColorOverlay, x10_ptr2) +
                            slot * sizeof(void*);
            uint32_t field;
            for (field = 0; field < desc->pointer_count; ++field) {
                if (desc->pointer_offsets[field] == offset) break;
            }
            if (field == desc->pointer_count) {
                fprintf(stderr, "missing color return slot: owner=%zu slot=%zu\n",
                        owner, slot);
                return 0;
            }
        }
    }
    return 1;
}

int main(int argc, char** argv)
{
    enum {
        MATCH_COUNT = 4,
        FRAME_COUNT = 180
    };
    MslCoreGameData game_data;
    MslCoreMatchConfig configs[MATCH_COUNT];
    MslCoreInput inputs[MATCH_COUNT] = { 0 };
    MslCoreMatch matches[MATCH_COUNT];
    MslCoreMatch reference;
    MslCoreCompare expected[MATCH_COUNT];
    MslCoreStageEvents stage_events = { 0 };
    size_t match_used[MATCH_COUNT];
    size_t match_allocations[MATCH_COUNT];
    size_t game_used;
    size_t game_allocations;
    uint64_t game_data_hash;
    uint64_t game_arena_hash;
    uint64_t final_game_data_hash;
    uint64_t final_game_arena_hash;
    uint32_t raw_files;
    uint32_t archives;
    uint8_t* global_snapshot;
    uint8_t* game_data_snapshot;
    size_t global_size;
#ifdef MSL_CORE_NATIVE
    size_t native_dat_used;
    uint64_t native_dat_hash;
    uint64_t final_native_dat_hash;
#endif
    int match_index;
    int frame;

    if (!color_return_slots_registered()) return 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA\n", argv[0]);
        return 2;
    }
    config_init(&configs[0], 2, 9, 4);
    config_init(&configs[1], 2, 1, 2);
    config_init(&configs[2], 28, 15, 3);
    config_init(&configs[3], 28, 22, 1);
    inputs[0].p[0].main_x = 80;
    inputs[1].p[0].main_x = -80;
    inputs[2].p[0].main_x = 40;
    inputs[3].p[0].main_x = -40;
    if (msl_core_game_data_init(&game_data, argv[1]) != 0) {
        return 1;
    }
    game_used = game_data.memory.used;
    game_allocations = game_data.memory.allocation_count;
    raw_files = game_data.files.raw_count;
    archives = game_data.files.archive_count;
#ifdef MSL_CORE_NATIVE
    native_dat_used = game_data.native_dat.arena_used;
    native_dat_hash =
        hash_bytes(game_data.native_dat.arena, game_data.native_dat.arena_used);
#endif
    game_data_hash = hash_bytes(&game_data, sizeof(game_data));
    game_arena_hash = hash_bytes(game_data.memory.arena, game_data.memory.used);
    game_data_snapshot = malloc(sizeof(game_data));
    if (game_data_snapshot == NULL) {
        return 1;
    }
    memcpy(game_data_snapshot, &game_data, sizeof(game_data));

    // Establish each result before constructing a different configuration.
    // Subsequent interleaving therefore detects both mutable shared-data
    // leakage and process-global runtime ownership.
    for (match_index = 0; match_index < MATCH_COUNT; ++match_index) {
        if (msl_core_match_init(&reference, &game_data, &configs[match_index],
                                &inputs[match_index]) != 0)
        {
            return 1;
        }
        for (frame = 0; frame < FRAME_COUNT; ++frame) {
            if (msl_core_match_step(&reference, &inputs[match_index], 1,
                                    &stage_events) != 0)
            {
                return 1;
            }
        }
        expected[match_index] = *msl_core_match_output(&reference);
        if (!observation_projection_matches(&reference,
                                            "isolated stepping"))
        {
            return 1;
        }
        if (!raw_objects_have_no_managed_pointers(&reference,
                                                  "isolated stepping"))
        {
            return 1;
        }
        if (reference.memory.used == 0 || !reference.memory.sealed ||
            game_data.memory.used != game_used ||
            game_data.memory.allocation_count != game_allocations ||
            game_data.files.raw_count != raw_files ||
            game_data.files.archive_count != archives
#ifdef MSL_CORE_NATIVE
            || game_data.native_dat.arena_used != native_dat_used
#endif
        )
        {
            fprintf(
                stderr,
                "isolated stepping changed sealed allocation/data owners\n");
            return 1;
        }
        msl_memory_context_destroy(&reference.memory);
    }

    // All class/source initialization has now been exercised. Gameplay Match
    // construction, stepping, and reset must mutate only explicitly bound
    // MatchState/TLS, never the executable's process-global data segment.
    global_size = writable_data_size();
    global_snapshot = malloc(global_size);
    if (global_snapshot == NULL) {
        return 1;
    }
    memcpy(global_snapshot, writable_data_start(), global_size);

    for (match_index = 0; match_index < MATCH_COUNT; ++match_index) {
        if (msl_core_match_init(&matches[match_index], &game_data,
                                &configs[match_index],
                                &inputs[match_index]) != 0)
        {
            return 1;
        }
        match_used[match_index] = matches[match_index].memory.used;
        match_allocations[match_index] =
            matches[match_index].memory.allocation_count;
    }
    if (!writable_globals_unchanged(global_snapshot, global_size,
                                    "match construction"))
    {
        return 1;
    }
    for (frame = 0; frame < FRAME_COUNT; ++frame) {
        for (match_index = 0; match_index < MATCH_COUNT; ++match_index) {
            if (msl_core_match_step(&matches[match_index],
                                    &inputs[match_index], 1,
                                    &stage_events) != 0)
            {
                return 1;
            }
        }
    }

    for (match_index = 0; match_index < MATCH_COUNT; ++match_index) {
        if (!raw_objects_have_no_managed_pointers(&matches[match_index],
                                                  "interleaved stepping"))
        {
            return 1;
        }
        if (!matches[match_index].memory.sealed ||
            matches[match_index].memory.used != match_used[match_index] ||
            matches[match_index].memory.allocation_count !=
                match_allocations[match_index])
        {
            fprintf(stderr,
                    "match %d changed storage while stepping: "
                    "used=%zu/%zu allocations=%zu/%zu\n",
                    match_index, matches[match_index].memory.used,
                    match_used[match_index],
                    matches[match_index].memory.allocation_count,
                    match_allocations[match_index]);
            return 1;
        }
        const uint8_t* actual =
            (const uint8_t*) msl_core_match_output(&matches[match_index]);
        const uint8_t* wanted = (const uint8_t*) &expected[match_index];
        size_t byte;
        if (memcmp(actual, wanted, sizeof(MslCoreCompare)) == 0) {
            continue;
        }
        for (byte = 0;
             byte < sizeof(MslCoreCompare) && actual[byte] == wanted[byte];
             ++byte)
        {
        }
        fprintf(stderr,
                "interleaved match %d differs from isolated stepping: "
                "first_byte=%zu:%02x/%02x pos=%08x/%08x "
                "action=%u/%u percent=%08x/%08x\n",
                match_index, byte,
                byte < sizeof(MslCoreCompare) ? actual[byte] : 0,
                byte < sizeof(MslCoreCompare) ? wanted[byte] : 0,
                *(const uint32_t*) &matches[match_index].output.pos_x[0],
                *(const uint32_t*) &expected[match_index].pos_x[0],
                matches[match_index].output.action_id[0],
                expected[match_index].action_id[0],
                *(const uint32_t*) &matches[match_index].output.percent[0],
                *(const uint32_t*) &expected[match_index].percent[0]);
        return 1;
    }
    if (!writable_globals_unchanged(global_snapshot, global_size,
                                    "interleaved stepping"))
    {
        return 1;
    }
    for (match_index = 0; match_index < MATCH_COUNT; ++match_index) {
        if (msl_core_match_reset(&matches[match_index], &game_data,
                                 &configs[match_index],
                                 &inputs[match_index]) != 0)
        {
            return 1;
        }
        for (frame = 0; frame < FRAME_COUNT; ++frame) {
            if (msl_core_match_step(&matches[match_index],
                                    &inputs[match_index], 1,
                                    &stage_events) != 0)
            {
                return 1;
            }
        }
        if (memcmp(msl_core_match_output(&matches[match_index]),
                   &expected[match_index], sizeof(MslCoreCompare)) != 0)
        {
            fprintf(stderr, "reset match %d differs from fresh stepping\n",
                    match_index);
            return 1;
        }
        if (!raw_objects_have_no_managed_pointers(&matches[match_index],
                                                  "match reset"))
        {
            return 1;
        }
        if (!matches[match_index].memory.sealed ||
            matches[match_index].memory.used != match_used[match_index] ||
            matches[match_index].memory.allocation_count !=
                match_allocations[match_index])
        {
            fprintf(stderr,
                    "reset match %d changed its initialized storage shape\n",
                    match_index);
            return 1;
        }
    }
    final_game_data_hash = hash_bytes(&game_data, sizeof(game_data));
    final_game_arena_hash =
        hash_bytes(game_data.memory.arena, game_data.memory.used);
#ifdef MSL_CORE_NATIVE
    final_native_dat_hash = hash_bytes(game_data.native_dat.arena,
                                       game_data.native_dat.arena_used);
#endif
    if (game_data.memory.used != game_used ||
        game_data.memory.allocation_count != game_allocations ||
        game_data.files.raw_count != raw_files ||
        game_data.files.archive_count != archives ||
        final_game_data_hash != game_data_hash ||
        final_game_arena_hash != game_arena_hash
#ifdef MSL_CORE_NATIVE
        || game_data.native_dat.arena_used != native_dat_used ||
        final_native_dat_hash != native_dat_hash
#endif
    )
    {
        size_t byte;
        for (byte = 0; byte < sizeof(game_data) &&
                       game_data_snapshot[byte] ==
                           ((const uint8_t*) &game_data)[byte];
             ++byte)
        {
        }
        fprintf(stderr,
                "runtime changed immutable GameData ownership: "
                "struct=%016llx/%016llx arena=%016llx/%016llx"
#ifdef MSL_CORE_NATIVE
                " native=%016llx/%016llx"
#endif
                " first_byte=%zu:%02x/%02x\n",
                (unsigned long long) game_data_hash,
                (unsigned long long) final_game_data_hash,
                (unsigned long long) game_arena_hash,
                (unsigned long long) final_game_arena_hash,
#ifdef MSL_CORE_NATIVE
                (unsigned long long) native_dat_hash,
                (unsigned long long) final_native_dat_hash,
#endif
                byte,
                byte < sizeof(game_data) ? game_data_snapshot[byte] : 0,
                byte < sizeof(game_data)
                    ? ((const uint8_t*) &game_data)[byte]
                    : 0
        );
        return 1;
    }
    if (!writable_globals_unchanged(global_snapshot, global_size,
                                    "match reset"))
    {
        return 1;
    }
    free(global_snapshot);
    free(game_data_snapshot);
    return 0;
}
