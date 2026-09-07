#include "runtime/batch.h"

#include "runtime/observation.h"
#include "runtime/savestate.h"
#include "runtime/scalar.h"
#include "runtime/subsystem_profile.h"
#include "runtime/viewer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { MSL_CORE_BATCH_MATCH_LIMIT = 16384 };

struct MslCoreBatch {
    const MslCoreGameData* game_data;
    MslCoreMatch* matches;
    uint8_t* match_arenas;
    uint32_t* reset_index;
    uint8_t* initialized;
    uint32_t match_count;
    uint32_t reset_index_capacity;
};

static inline int selected(const uint8_t* mask, size_t stride, uint32_t index)
{
    return mask == NULL || mask[(size_t) index * stride] != 0;
}

static const void* row_const(const void* rows, size_t stride, uint32_t index)
{
    return (const uint8_t*) rows + (size_t) index * stride;
}

static void* row_mutable(void* rows, size_t stride, uint32_t index)
{
    return (uint8_t*) rows + (size_t) index * stride;
}

static int supported_stage(uint32_t stage_id)
{
    return stage_id == MSL_STAGE_FOUNTAIN_OF_DREAMS ||
           stage_id == MSL_STAGE_POKEMON_STADIUM ||
           stage_id == MSL_STAGE_YOSHIS_STORY ||
           stage_id == MSL_STAGE_DREAM_LAND_N64 ||
           stage_id == MSL_STAGE_BATTLEFIELD ||
           stage_id == MSL_STAGE_FINAL_DESTINATION;
}

static int supported_character(uint8_t char_id)
{
    return char_id == MSL_CHARACTER_FOX ||
           char_id == MSL_CHARACTER_CAPTAIN_FALCON ||
           char_id == MSL_CHARACTER_SHEIK ||
           char_id == MSL_CHARACTER_PEACH ||
           char_id == MSL_CHARACTER_ICE_CLIMBERS ||
           char_id == MSL_CHARACTER_DONKEY_KONG ||
           char_id == MSL_CHARACTER_GANONDORF ||
           char_id == MSL_CHARACTER_YOSHI ||
           char_id == MSL_CHARACTER_BOWSER ||
           char_id == MSL_CHARACTER_PIKACHU ||
           char_id == MSL_CHARACTER_SAMUS ||
           char_id == MSL_CHARACTER_JIGGLYPUFF ||
           char_id == MSL_CHARACTER_LUIGI ||
           char_id == MSL_CHARACTER_MARIO ||
           char_id == MSL_CHARACTER_DRMARIO ||
           char_id == MSL_CHARACTER_MARTH ||
           char_id == MSL_CHARACTER_ZELDA ||
           char_id == MSL_CHARACTER_FALCO;
}

static int valid_config(const MslCoreMatchConfig* config)
{
    uint8_t source_slots[MSL_CORE_MAX_PLAYERS];
    uint32_t player;
    if (config == NULL || !supported_stage(config->stage_id) ||
        (config->num_players < 2 || config->num_players > 4) ||
        !isfinite(config->match_damage_ratio) ||
        config->match_damage_ratio < 0.0F)
    {
        return 0;
    }
    for (player = 0; player < config->num_players; ++player) {
        uint8_t encoded = config->players[player].facing_and_port;
        uint8_t port = encoded >> 1;
        uint32_t previous;
        source_slots[player] = port == 0 ? (uint8_t) player : (uint8_t) (port - 1);
        if (!supported_character(config->players[player].char_id) ||
            config->players[player].handicap > 9 ||
            config->players[player].start_percent > 100 ||
            (config->is_teams && config->players[player].team_id > 2) ||
            source_slots[player] >= MSL_CORE_MAX_PLAYERS)
        {
            return 0;
        }
        for (previous = 0; previous < player; ++previous) {
            if (source_slots[previous] == source_slots[player]) {
                return 0;
            }
        }
    }
    return 1;
}

MslCoreResult msl_core_game_data_create(
    const char* data_root, MslCoreGameData** out_game_data)
{
    MslCoreGameData* game_data;
    if (data_root == NULL || data_root[0] == '\0' || out_game_data == NULL) {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    *out_game_data = NULL;
    game_data = calloc(1, sizeof(*game_data));
    if (game_data == NULL) {
        return MSL_CORE_OUT_OF_MEMORY;
    }
    if (msl_core_game_data_init(game_data, data_root) != 0) {
        msl_core_game_data_deinit(game_data);
        free(game_data);
        return MSL_CORE_INVALID_STATE;
    }
    *out_game_data = game_data;
    return MSL_CORE_OK;
}

void msl_core_game_data_destroy(MslCoreGameData* game_data)
{
    if (game_data == NULL) {
        return;
    }
    msl_core_game_data_deinit(game_data);
    free(game_data);
}

MslCoreResult msl_core_batch_create(
    const MslCoreGameData* game_data, uint32_t match_count,
    MslCoreBatch** out_batch)
{
    MslCoreBatch* batch;
    size_t match_capacity;
    uint32_t i;
    if (game_data == NULL || out_batch == NULL || match_count == 0 ||
        match_count > MSL_CORE_BATCH_MATCH_LIMIT)
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    *out_batch = NULL;
    batch = calloc(1, sizeof(*batch));
    if (batch == NULL) {
        return MSL_CORE_OUT_OF_MEMORY;
    }
    batch->game_data = game_data;
    batch->match_count = match_count;
    batch->matches = calloc(match_count, sizeof(*batch->matches));
    batch->initialized = calloc(match_count, sizeof(*batch->initialized));
    batch->reset_index_capacity = 1;
    while (batch->reset_index_capacity < match_count * 2) {
        batch->reset_index_capacity *= 2;
    }
    batch->reset_index = calloc(batch->reset_index_capacity,
                                sizeof(*batch->reset_index));
    batch->match_arenas = msl_memory_map_match_arenas(match_count);
    if (batch->matches == NULL || batch->initialized == NULL ||
        batch->reset_index == NULL || batch->match_arenas == NULL)
    {
        msl_core_batch_destroy(batch);
        return MSL_CORE_OUT_OF_MEMORY;
    }
    match_capacity = msl_memory_match_capacity();
    for (i = 0; i < match_count; ++i) {
        if (msl_core_match_storage_bind(
                &batch->matches[i],
                batch->match_arenas + (size_t) i * match_capacity,
                match_capacity) != 0)
        {
            msl_core_batch_destroy(batch);
            return MSL_CORE_OUT_OF_MEMORY;
        }
    }
    *out_batch = batch;
    return MSL_CORE_OK;
}

void msl_core_batch_destroy(MslCoreBatch* batch)
{
    uint32_t i;
    if (batch == NULL) {
        return;
    }
    if (batch->matches != NULL) {
        for (i = 0; i < batch->match_count; ++i) {
            msl_core_match_destroy(&batch->matches[i]);
        }
    }
    msl_memory_unmap_match_arenas(batch->match_arenas, batch->match_count);
    free(batch->reset_index);
    free(batch->initialized);
    free(batch->matches);
    free(batch);
}

static uint32_t hash_config(const MslCoreMatchConfig* config)
{
    const uint8_t* bytes = (const uint8_t*) config;
    uint32_t hash = UINT32_C(2166136261);
    size_t i;
    for (i = 0; i < sizeof(*config); ++i) {
        hash ^= bytes[i];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

uint32_t msl_core_batch_match_count(const MslCoreBatch* batch)
{
    return batch != NULL ? batch->match_count : 0;
}

static MslCoreResult validate_mask(const MslCoreBatch* batch,
                                   const uint8_t* mask, size_t stride)
{
    if (batch == NULL || (mask != NULL && stride < sizeof(uint8_t))) {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_reset_matches(
    MslCoreBatch* batch, const MslCoreMatchConfig* configs,
    size_t config_stride, const uint8_t* match_mask, size_t mask_stride)
{
    MslCoreInput previous = { 0 };
    uint32_t i;
    if (validate_mask(batch, match_mask, mask_stride) != MSL_CORE_OK ||
        configs == NULL || config_stride < sizeof(*configs))
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < batch->match_count; ++i) {
        const MslCoreMatchConfig* config =
            row_const(configs, config_stride, i);
        if (selected(match_mask, mask_stride, i) && !valid_config(config)) {
            return MSL_CORE_INVALID_ARGUMENT;
        }
    }
    memset(batch->reset_index, 0,
           (size_t) batch->reset_index_capacity *
               sizeof(*batch->reset_index));
    for (i = 0; i < batch->match_count; ++i) {
        const MslCoreMatchConfig* config =
            row_const(configs, config_stride, i);
        if (selected(match_mask, mask_stride, i)) {
            uint32_t slot =
                hash_config(config) & (batch->reset_index_capacity - 1);
            uint32_t source = UINT32_MAX;
            while (batch->reset_index[slot] != 0) {
                uint32_t candidate = batch->reset_index[slot] - 1;
                if (memcmp(config, &batch->matches[candidate].config,
                           sizeof(*config)) == 0)
                {
                    source = candidate;
                    break;
                }
                slot = (slot + 1) & (batch->reset_index_capacity - 1);
            }
            if (source == UINT32_MAX) {
                if (msl_core_match_reset(&batch->matches[i], batch->game_data,
                                         config, &previous) != 0)
                {
                    return MSL_CORE_INVALID_STATE;
                }
                batch->reset_index[slot] = i + 1;
            } else {
                batch->matches[i].game_data = batch->game_data;
                if (msl_core_match_copy(&batch->matches[i],
                                        &batch->matches[source]) != 0)
                {
                    return MSL_CORE_INVALID_STATE;
                }
            }
            batch->initialized[i] = 1;
        }
    }
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_step_matches(
    MslCoreBatch* batch, const MslCoreInput* inputs, size_t input_stride,
    const uint8_t* match_mask, size_t mask_stride)
{
    static const MslCoreStageEvents no_stage_events = { 0 };
    uint32_t i;
#ifdef MSL_SUBSYSTEM_PROFILE
    uint64_t started = msl_profile_cycles();
#endif
    if (validate_mask(batch, match_mask, mask_stride) != MSL_CORE_OK ||
        inputs == NULL || input_stride < sizeof(*inputs))
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < batch->match_count; ++i) {
        if (selected(match_mask, mask_stride, i) && !batch->initialized[i]) {
            return MSL_CORE_INVALID_STATE;
        }
    }
    // Until an owner has a real cross-environment kernel, interleaving its
    // scalar source callbacks only evicts the current Match and republishes
    // the full hosted context. Keep one Match resident for its complete
    // source scheduler; later batch kernels can cut in at explicit owner
    // boundaries instead of paying a generic callback-grouping tax.
    // refs/melee/src/sysdolphin/baselib/gobj.c::HSD_GObj_80390CFC
    for (i = 0; i < batch->match_count; ++i) {
        if (selected(match_mask, mask_stride, i)) {
            MslCoreMatch* match = &batch->matches[i];
            uint32_t frame_seed = match->random_seed;
            if (msl_core_match_step(match, row_const(inputs, input_stride, i),
                                    frame_seed, &no_stage_events) != 0)
            {
                return MSL_CORE_INVALID_STATE;
            }
        }
    }
#ifdef MSL_SUBSYSTEM_PROFILE
    msl_profile_add(MSL_PROFILE_STEP,
                         msl_profile_cycles() - started);
#endif
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_write_state(
    const MslCoreBatch* batch, MslCoreCompare* output, size_t output_stride,
    const uint8_t* match_mask, size_t mask_stride)
{
    uint32_t i;
    if (validate_mask(batch, match_mask, mask_stride) != MSL_CORE_OK ||
        output == NULL || output_stride < sizeof(*output))
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < batch->match_count; ++i) {
        if (selected(match_mask, mask_stride, i)) {
            if (!batch->initialized[i]) {
                return MSL_CORE_INVALID_STATE;
            }
            memcpy(row_mutable(output, output_stride, i),
                   msl_core_match_output(&batch->matches[i]), sizeof(*output));
        }
    }
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_write_terminal(
    const MslCoreBatch* batch, MslCoreTerminal* output, size_t output_stride,
    int32_t max_frame_id, const uint8_t* match_mask, size_t mask_stride)
{
    uint32_t i;
#ifdef MSL_SUBSYSTEM_PROFILE
    uint64_t started = msl_profile_cycles();
#endif
    if (validate_mask(batch, match_mask, mask_stride) != MSL_CORE_OK ||
        output == NULL || output_stride < sizeof(*output))
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < batch->match_count; ++i) {
        if (selected(match_mask, mask_stride, i)) {
            if (!batch->initialized[i]) {
                return MSL_CORE_INVALID_STATE;
            }
            msl_core_match_write_terminal(
                &batch->matches[i], max_frame_id,
                row_mutable(output, output_stride, i));
        }
    }
#ifdef MSL_SUBSYSTEM_PROFILE
    msl_profile_add(MSL_PROFILE_TERMINAL,
                         msl_profile_cycles() - started);
#endif
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_write_terminals(
    const MslCoreBatch* batch, MslCoreTerminal* output, size_t output_stride,
    const int32_t* max_frame_ids, size_t max_frame_stride,
    const uint8_t* match_mask, size_t mask_stride)
{
    uint32_t i;
    if (validate_mask(batch, match_mask, mask_stride) != MSL_CORE_OK ||
        output == NULL || output_stride < sizeof(*output) ||
        max_frame_ids == NULL || max_frame_stride < sizeof(*max_frame_ids))
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < batch->match_count; ++i) {
        if (selected(match_mask, mask_stride, i)) {
            const int32_t* max_frame =
                row_const(max_frame_ids, max_frame_stride, i);
            if (!batch->initialized[i]) {
                return MSL_CORE_INVALID_STATE;
            }
            msl_core_match_write_terminal(
                &batch->matches[i], *max_frame,
                row_mutable(output, output_stride, i));
        }
    }
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_write_observation(
    const MslCoreBatch* batch, const uint8_t* viewpoint_players,
    size_t viewpoint_stride, MslCoreObservation* output,
    size_t output_stride, const uint8_t* match_mask, size_t mask_stride)
{
    uint32_t i;
#ifdef MSL_SUBSYSTEM_PROFILE
    uint64_t started = msl_profile_cycles();
#endif
    if (validate_mask(batch, match_mask, mask_stride) != MSL_CORE_OK ||
        viewpoint_players == NULL || viewpoint_stride < sizeof(uint8_t) ||
        output == NULL || output_stride < sizeof(*output))
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < batch->match_count; ++i) {
        if (selected(match_mask, mask_stride, i)) {
            uint8_t viewpoint = viewpoint_players[(size_t) i * viewpoint_stride];
            if (!batch->initialized[i]) {
                return MSL_CORE_INVALID_STATE;
            }
            if (viewpoint >= batch->matches[i].config.num_players) {
                return MSL_CORE_INVALID_ARGUMENT;
            }
        }
    }
    for (i = 0; i < batch->match_count; ++i) {
        if (selected(match_mask, mask_stride, i)) {
            uint8_t viewpoint = viewpoint_players[(size_t) i * viewpoint_stride];
            if (msl_core_match_write_observation(
                    &batch->matches[i], viewpoint,
                    row_mutable(output, output_stride, i)) != 0)
            {
                return MSL_CORE_INVALID_STATE;
            }
        }
    }
#ifdef MSL_SUBSYSTEM_PROFILE
    msl_profile_add(MSL_PROFILE_OBSERVATION,
                         msl_profile_cycles() - started);
#endif
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_write_viewer(
    const MslCoreBatch* batch, MslCoreViewerState* output,
    size_t output_stride, const uint8_t* match_mask, size_t mask_stride)
{
    uint32_t i;
    if (validate_mask(batch, match_mask, mask_stride) != MSL_CORE_OK ||
        output == NULL || output_stride < sizeof(*output))
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < batch->match_count; ++i) {
        if (selected(match_mask, mask_stride, i)) {
            if (!batch->initialized[i]) {
                return MSL_CORE_INVALID_STATE;
            }
            msl_core_match_write_viewer(&batch->matches[i],
                                        row_mutable(output, output_stride, i));
        }
    }
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_copy_matches(
    MslCoreBatch* destination, const MslCoreBatch* source,
    const uint32_t* destination_indices, const uint32_t* source_indices,
    uint32_t count)
{
    uint32_t i;
    uint32_t j;
    if (destination == NULL || source == NULL ||
        (count != 0 &&
         (destination_indices == NULL || source_indices == NULL)))
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    for (i = 0; i < count; ++i) {
        if (destination_indices[i] >= destination->match_count ||
            source_indices[i] >= source->match_count ||
            !source->initialized[source_indices[i]])
        {
            return MSL_CORE_INVALID_ARGUMENT;
        }
        if (destination == source &&
            destination_indices[i] != source_indices[i])
        {
            for (j = 0; j < count; ++j) {
                if (destination_indices[i] == source_indices[j]) {
                    return MSL_CORE_INVALID_ARGUMENT;
                }
            }
        }
    }
    for (i = 0; i < count; ++i) {
        MslCoreMatch* dst = &destination->matches[destination_indices[i]];
        const MslCoreMatch* src = &source->matches[source_indices[i]];
        if (dst != src) {
            dst->game_data = destination->game_data;
            if (msl_core_match_copy(dst, src) != 0) {
                return MSL_CORE_INCOMPATIBLE;
            }
        }
        destination->initialized[destination_indices[i]] = 1;
    }
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_match_save_size(
    const MslCoreBatch* batch, uint32_t match_index, size_t* required_size)
{
    if (batch == NULL || required_size == NULL ||
        match_index >= batch->match_count)
    {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    if (!batch->initialized[match_index]) {
        return MSL_CORE_INVALID_STATE;
    }
    *required_size = msl_core_match_save_size(&batch->matches[match_index]);
    return MSL_CORE_OK;
}

MslCoreResult msl_core_batch_save_match(
    const MslCoreBatch* batch, uint32_t match_index, void* buffer,
    size_t buffer_size, size_t* written)
{
    if (batch == NULL || buffer == NULL || match_index >= batch->match_count) {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    if (!batch->initialized[match_index]) {
        return MSL_CORE_INVALID_STATE;
    }
    return msl_core_match_save(&batch->matches[match_index], buffer,
                               buffer_size, written) == 0
               ? MSL_CORE_OK
               : MSL_CORE_INVALID_ARGUMENT;
}

MslCoreResult msl_core_batch_restore_match(
    MslCoreBatch* batch, uint32_t match_index, const void* buffer,
    size_t buffer_size)
{
    MslCoreMatch* match;
    if (batch == NULL || buffer == NULL || match_index >= batch->match_count) {
        return MSL_CORE_INVALID_ARGUMENT;
    }
    match = &batch->matches[match_index];
    match->game_data = batch->game_data;
    if (msl_core_match_restore(match, buffer, buffer_size) != 0) {
        return MSL_CORE_INCOMPATIBLE;
    }
    batch->initialized[match_index] = 1;
    return MSL_CORE_OK;
}
