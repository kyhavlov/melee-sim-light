#ifndef MSL_CORE_API_H
#define MSL_CORE_API_H

#include "runtime/wire.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MSL_CORE_SHARED) && defined(__GNUC__)
#define MSL_CORE_API __attribute__((visibility("default")))
#else
#define MSL_CORE_API
#endif

typedef struct MslCoreGameData MslCoreGameData;
typedef struct MslCoreBatch MslCoreBatch;

typedef enum MslCoreResult {
    MSL_CORE_OK = 0,
    MSL_CORE_INVALID_ARGUMENT = 1,
    MSL_CORE_OUT_OF_MEMORY = 2,
    MSL_CORE_INVALID_STATE = 3,
    MSL_CORE_INCOMPATIBLE = 4,
} MslCoreResult;

typedef enum MslCoreCharacter {
    MSL_CORE_CHARACTER_FOX = 1,
    MSL_CORE_CHARACTER_CAPTAIN_FALCON = 2,
    MSL_CORE_CHARACTER_SHEIK = 7,
    MSL_CORE_CHARACTER_PEACH = 9,
    MSL_CORE_CHARACTER_JIGGLYPUFF = 15,
    MSL_CORE_CHARACTER_MARTH = 18,
    MSL_CORE_CHARACTER_ZELDA = 19,
    MSL_CORE_CHARACTER_FALCO = 22,
} MslCoreCharacter;

typedef enum MslCoreStage {
    MSL_CORE_STAGE_FOUNTAIN_OF_DREAMS = 2,
    MSL_CORE_STAGE_POKEMON_STADIUM = 3,
    MSL_CORE_STAGE_YOSHIS_STORY = 8,
    MSL_CORE_STAGE_DREAM_LAND_N64 = 28,
    MSL_CORE_STAGE_BATTLEFIELD = 31,
    MSL_CORE_STAGE_FINAL_DESTINATION = 32,
} MslCoreStage;

typedef enum MslCoreButton {
    MSL_CORE_BUTTON_D_DOWN = 0x0004,
    MSL_CORE_BUTTON_D_UP = 0x0008,
    MSL_CORE_BUTTON_Z = 0x0010,
    MSL_CORE_BUTTON_R = 0x0020,
    MSL_CORE_BUTTON_L = 0x0040,
    MSL_CORE_BUTTON_A = 0x0100,
    MSL_CORE_BUTTON_B = 0x0200,
    MSL_CORE_BUTTON_X = 0x0400,
    MSL_CORE_BUTTON_Y = 0x0800,
    MSL_CORE_BUTTON_START = 0x1000,
} MslCoreButton;

// The first production projection intentionally retains the already locked
// gameplay wire. Replay authority and forensic comparison APIs remain private.
typedef MslCoreCompare MslCoreState;

MSL_CORE_API MslCoreResult msl_core_game_data_create(
    const char* data_root, MslCoreGameData** out_game_data);
MSL_CORE_API void msl_core_game_data_destroy(MslCoreGameData* game_data);

MSL_CORE_API MslCoreResult msl_core_batch_create(
    const MslCoreGameData* game_data, uint32_t match_count,
    MslCoreBatch** out_batch);
MSL_CORE_API void msl_core_batch_destroy(MslCoreBatch* batch);
MSL_CORE_API uint32_t msl_core_batch_match_count(const MslCoreBatch* batch);

// A NULL match mask selects every match. Otherwise each byte-strided mask row
// is zero (keep current state) or nonzero (select).
MSL_CORE_API MslCoreResult msl_core_batch_reset_matches(MslCoreBatch* batch,
                                                        const MslCoreMatchConfig* configs,
                                                        size_t config_stride,
                                                        const uint8_t* match_mask,
                                                        size_t mask_stride);
MSL_CORE_API MslCoreResult msl_core_batch_step_matches(MslCoreBatch* batch,
                                                       const MslCoreInput* inputs,
                                                       size_t input_stride,
                                                       const uint8_t* match_mask,
                                                       size_t mask_stride);
MSL_CORE_API MslCoreResult msl_core_batch_write_state(
    const MslCoreBatch* batch, MslCoreState* output, size_t output_stride,
    const uint8_t* match_mask, size_t mask_stride);
MSL_CORE_API MslCoreResult msl_core_batch_write_observation(
    const MslCoreBatch* batch, const uint8_t* viewpoint_players,
    size_t viewpoint_stride, MslCoreObservation* output,
    size_t output_stride, const uint8_t* match_mask, size_t mask_stride);
MSL_CORE_API MslCoreResult msl_core_batch_write_terminal(
    const MslCoreBatch* batch, MslCoreTerminal* output, size_t output_stride,
    int32_t max_frame_id, const uint8_t* match_mask, size_t mask_stride);
MSL_CORE_API MslCoreResult msl_core_batch_write_viewer(
    const MslCoreBatch* batch, MslCoreViewerState* output, size_t output_stride,
    const uint8_t* match_mask, size_t mask_stride);

MSL_CORE_API MslCoreResult msl_core_batch_copy_matches(MslCoreBatch* destination,
                                                       const MslCoreBatch* source,
                                                       const uint32_t* destination_indices,
                                                       const uint32_t* source_indices,
                                                       uint32_t count);
MSL_CORE_API MslCoreResult msl_core_batch_match_save_size(
    const MslCoreBatch* batch, uint32_t match_index, size_t* required_size);
MSL_CORE_API MslCoreResult msl_core_batch_save_match(
    const MslCoreBatch* batch, uint32_t match_index, void* buffer,
    size_t buffer_size, size_t* written);
MSL_CORE_API MslCoreResult msl_core_batch_restore_match(
    MslCoreBatch* batch, uint32_t match_index, const void* buffer,
    size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
