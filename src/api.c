#include "api.h"

#include "runtime/batch.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  MSL_BATCH_LIMIT = 16384,
  MSL_SAVE_MAGIC = 0x31564E45,
  MSL_SAVE_VERSION = 1,
};

typedef struct MslSaveHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t payload_size;
  int32_t max_frame;
  uint8_t viewpoint_player;
  uint8_t _reserved[3];
} MslSaveHeader;

struct MslBatch {
  MslCoreGameData* game_data;
  MslCoreBatch* runtime;
  MslCoreMatchConfig* configs;
  MslCoreInput* inputs;
  uint8_t* viewpoint_players;
  int32_t* max_frames;
  uint32_t size;
};

_Static_assert(MSL_OK == MSL_CORE_OK, "result values must agree");
_Static_assert(MSL_INCOMPATIBLE == MSL_CORE_INCOMPATIBLE, "result values must agree");
_Static_assert(sizeof(MslPlayerConfig) == 7, "MslPlayerConfig layout");
_Static_assert(sizeof(MslMatchConfig) == 52, "MslMatchConfig layout");
_Static_assert(sizeof(MslInputPlayer) == 8, "MslInputPlayer layout");
_Static_assert(sizeof(MslInput) == 32, "MslInput layout");
_Static_assert(sizeof(MslItem) == 48, "MslItem layout");
_Static_assert(sizeof(MslObservation) == 980, "MslObservation layout");
_Static_assert(sizeof(MslTerminal) == 16, "MslTerminal layout");
_Static_assert(sizeof(MslSaveHeader) == 20, "MslSaveHeader layout");

static MslResult public_result(MslCoreResult result) { return (MslResult)result; }

static int supported_stage(uint32_t stage) {
  return stage == MSL_STAGE_FOUNTAIN_OF_DREAMS || stage == MSL_STAGE_POKEMON_STADIUM ||
         stage == MSL_STAGE_YOSHIS_STORY || stage == MSL_STAGE_DREAM_LAND_N64 ||
         stage == MSL_STAGE_BATTLEFIELD || stage == MSL_STAGE_FINAL_DESTINATION;
}

static int supported_character(uint8_t character) {
  return character == MSL_CHARACTER_MARIO || character == MSL_CHARACTER_DRMARIO ||
         character == MSL_CHARACTER_FOX ||
         character == MSL_CHARACTER_MEWTWO ||
         character == MSL_CHARACTER_GAMEWATCH ||
         character == MSL_CHARACTER_CAPTAIN_FALCON ||
         character == MSL_CHARACTER_SHEIK || character == MSL_CHARACTER_PEACH ||
         character == MSL_CHARACTER_ICE_CLIMBERS ||
         character == MSL_CHARACTER_DONKEY_KONG ||
         character == MSL_CHARACTER_GANONDORF ||
         character == MSL_CHARACTER_YOSHI ||
         character == MSL_CHARACTER_BOWSER ||
         character == MSL_CHARACTER_PIKACHU ||
         character == MSL_CHARACTER_PICHU ||
         character == MSL_CHARACTER_SAMUS ||
         character == MSL_CHARACTER_NESS ||
         character == MSL_CHARACTER_LINK ||
         character == MSL_CHARACTER_YOUNG_LINK ||
         character == MSL_CHARACTER_JIGGLYPUFF || character == MSL_CHARACTER_LUIGI ||
         character == MSL_CHARACTER_MARTH ||
         character == MSL_CHARACTER_ROY ||
         character == MSL_CHARACTER_ZELDA || character == MSL_CHARACTER_FALCO;
}

static int translate_config(const MslMatchConfig* source, MslCoreMatchConfig* target) {
  uint8_t ports[MSL_MAX_PLAYERS];
  uint32_t player;

  if (source == NULL || target == NULL || !supported_stage(source->stage) ||
      (source->num_players < 2 || source->num_players > 4) || !isfinite(source->damage_ratio) ||
      source->damage_ratio <= 0.0F || source->stocks == 0 ||
      source->viewpoint_player >= source->num_players || source->is_teams > 1 ||
      source->friendly_fire > 1 || source->ucf_cardinals > 1) {
    return -1;
  }

  memset(target, 0, sizeof(*target));
  target->stage_id = source->stage;
  target->frame_id = -123;
  target->frame_pre_random_seed = source->random_seed;
  target->initial_random_seed = source->random_seed;
  target->match_damage_ratio = source->damage_ratio;
  target->num_players = source->num_players;
  target->is_teams = source->is_teams;
  target->friendly_fire = source->friendly_fire;
  target->stock_count = source->stocks;
  target->online_fnmsubs_zero = 1;
  target->brawl_offscreen_damage = 1;
  target->freeze_dead_up_fall_physics = 1;
  target->whispy_dead_fighter_fix = 1;
  target->ucf_cardinals_1_0_enabled = source->ucf_cardinals;
  target->ucf_shield_sdi_enabled = 1;
  target->ucf_sdi_enabled = 1;
  target->ucf_shield_drop_extended_enabled = 1;
  target->ucf_shield_drop_084_enabled = 1;

  for (player = 0; player < source->num_players; ++player) {
    const MslPlayerConfig* src = &source->players[player];
    MslCoreMatchPlayerConfig* dst = &target->players[player];
    uint8_t port;
    uint32_t previous;

    if (!supported_character(src->character) || src->team < MSL_TEAM_AUTO || src->team > 2 ||
        src->facing < MSL_FACING_LEFT || src->facing > MSL_FACING_RIGHT ||
        src->controller_port < MSL_CONTROLLER_PORT_AUTO ||
        src->controller_port >= MSL_MAX_PLAYERS || src->handicap < 1 || src->handicap > 9 ||
        src->start_percent > 100) {
      return -1;
    }

    port = src->controller_port == MSL_CONTROLLER_PORT_AUTO ? (uint8_t)player
                                                            : (uint8_t)src->controller_port;
    for (previous = 0; previous < player; ++previous) {
      if (ports[previous] == port) {
        return -1;
      }
    }
    ports[player] = port;
    dst->char_id = src->character;
    dst->team_id =
        src->team == MSL_TEAM_AUTO
            ? (source->is_teams ? (uint8_t)(player >= source->num_players / 2) : (uint8_t)player)
            : (uint8_t)src->team;
    dst->facing_and_port =
        (uint8_t)((src->facing == MSL_FACING_AUTO ? player == 0 : src->facing == MSL_FACING_RIGHT) |
                  ((port + 1) << 1));
    dst->costume_id = src->costume;
    dst->handicap = src->handicap;
    dst->start_percent = src->start_percent;
  }
  return 0;
}

static void translate_inputs(MslBatch* batch, const MslInput inputs[]) {
  uint32_t env;
  for (env = 0; env < batch->size; ++env) {
    uint32_t player;
    memset(&batch->inputs[env], 0, sizeof(batch->inputs[env]));
    for (player = 0; player < MSL_MAX_PLAYERS; ++player) {
      const MslInputPlayer* src = &inputs[env].players[player];
      MslCoreInputPlayer* dst = &batch->inputs[env].p[player];
      dst->buttons = src->buttons;
      dst->main_x = src->main_x;
      dst->main_y = src->main_y;
      dst->c_x = src->c_x;
      dst->c_y = src->c_y;
      dst->l = src->l;
      dst->r = src->r;
    }
  }
}

const char* msl_result_string(MslResult result) {
  switch (result) {
    case MSL_OK:
      return "ok";
    case MSL_INVALID_ARGUMENT:
      return "invalid argument";
    case MSL_OUT_OF_MEMORY:
      return "out of memory";
    case MSL_INVALID_STATE:
      return "invalid simulator state";
    case MSL_INCOMPATIBLE:
      return "incompatible data or savestate";
    default:
      return "unknown error";
  }
}

MslMatchConfig msl_match_config_default(void) {
  MslMatchConfig config;
  uint32_t player;

  memset(&config, 0, sizeof(config));
  config.stage = MSL_STAGE_FINAL_DESTINATION;
  config.random_seed = 1;
  config.max_frame = -1;
  config.damage_ratio = 1.0F;
  config.num_players = 2;
  config.stocks = 4;
  for (player = 0; player < MSL_MAX_PLAYERS; ++player) {
    config.players[player].character = player & 1 ? MSL_CHARACTER_FALCO : MSL_CHARACTER_FOX;
    config.players[player].team = MSL_TEAM_AUTO;
    config.players[player].facing = MSL_FACING_AUTO;
    config.players[player].controller_port = MSL_CONTROLLER_PORT_AUTO;
    config.players[player].handicap = 9;
  }
  return config;
}

MslResult msl_batch_create(const char* data_root, uint32_t batch_size, MslBatch** out_batch) {
  char raw_root[1024];
  MslBatch* batch;
  MslCoreResult core_result;
  size_t root_length;

  if (data_root == NULL || data_root[0] == '\0' || out_batch == NULL || batch_size == 0 ||
      batch_size > MSL_BATCH_LIMIT) {
    return MSL_INVALID_ARGUMENT;
  }
  root_length = strlen(data_root);
  if ((root_length == 3 && strcmp(data_root, "raw") == 0) ||
      (root_length >= 4 && strcmp(data_root + root_length - 4, "/raw") == 0)) {
    if (root_length >= sizeof(raw_root)) {
      return MSL_INVALID_ARGUMENT;
    }
    memcpy(raw_root, data_root, root_length + 1);
  } else if (snprintf(raw_root, sizeof(raw_root), "%s/raw", data_root) >= (int)sizeof(raw_root)) {
    return MSL_INVALID_ARGUMENT;
  }
  *out_batch = NULL;
  batch = calloc(1, sizeof(*batch));
  if (batch == NULL) {
    return MSL_OUT_OF_MEMORY;
  }
  batch->size = batch_size;
  batch->configs = calloc(batch_size, sizeof(*batch->configs));
  batch->inputs = calloc(batch_size, sizeof(*batch->inputs));
  batch->viewpoint_players = calloc(batch_size, sizeof(*batch->viewpoint_players));
  batch->max_frames = calloc(batch_size, sizeof(*batch->max_frames));
  if (batch->configs == NULL || batch->inputs == NULL || batch->viewpoint_players == NULL ||
      batch->max_frames == NULL) {
    msl_batch_destroy(batch);
    return MSL_OUT_OF_MEMORY;
  }
  core_result = msl_core_game_data_create(raw_root, &batch->game_data);
  if (core_result != MSL_CORE_OK) {
    msl_batch_destroy(batch);
    return public_result(core_result);
  }
  core_result = msl_core_batch_create(batch->game_data, batch_size, &batch->runtime);
  if (core_result != MSL_CORE_OK) {
    msl_batch_destroy(batch);
    return public_result(core_result);
  }
  *out_batch = batch;
  return MSL_OK;
}

void msl_batch_destroy(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  msl_core_batch_destroy(batch->runtime);
  msl_core_game_data_destroy(batch->game_data);
  free(batch->max_frames);
  free(batch->viewpoint_players);
  free(batch->inputs);
  free(batch->configs);
  free(batch);
}

uint32_t msl_batch_size(const MslBatch* batch) { return batch != NULL ? batch->size : 0; }

MslResult msl_batch_reset(MslBatch* batch, const MslMatchConfig configs[],
                          const uint8_t reset_mask[], MslObservation observations[]) {
  MslCoreResult result;
  uint32_t env;

  if (batch == NULL || configs == NULL || observations == NULL) {
    return MSL_INVALID_ARGUMENT;
  }
  for (env = 0; env < batch->size; ++env) {
    if ((reset_mask == NULL || reset_mask[env] != 0) &&
        translate_config(&configs[env], &batch->configs[env]) != 0) {
      return MSL_INVALID_ARGUMENT;
    }
  }
  result = msl_core_batch_reset_matches(batch->runtime, batch->configs, sizeof(batch->configs[0]),
                                        reset_mask, sizeof(reset_mask[0]));
  if (result != MSL_CORE_OK) {
    return public_result(result);
  }
  for (env = 0; env < batch->size; ++env) {
    if (reset_mask == NULL || reset_mask[env] != 0) {
      batch->viewpoint_players[env] = configs[env].viewpoint_player;
      batch->max_frames[env] = configs[env].max_frame;
    }
  }
  return public_result(msl_core_batch_write_observation(
      batch->runtime, batch->viewpoint_players, sizeof(batch->viewpoint_players[0]), observations,
      sizeof(observations[0]), reset_mask, sizeof(reset_mask[0])));
}

MslResult msl_batch_observe(const MslBatch* batch, MslObservation observations[],
                            MslTerminal terminals[]) {
  MslCoreResult result;
  if (batch == NULL || observations == NULL || terminals == NULL) {
    return MSL_INVALID_ARGUMENT;
  }
  result = msl_core_batch_write_observation(batch->runtime, batch->viewpoint_players,
                                            sizeof(batch->viewpoint_players[0]), observations,
                                            sizeof(observations[0]), NULL, 0);
  if (result != MSL_CORE_OK) {
    return public_result(result);
  }
  return public_result(msl_core_batch_write_terminals(batch->runtime, terminals,
                                                      sizeof(terminals[0]), batch->max_frames,
                                                      sizeof(batch->max_frames[0]), NULL, 0));
}

MslResult msl_batch_step_masked(MslBatch* batch, const MslInput inputs[],
                                const uint8_t step_mask[], MslObservation observations[],
                                MslTerminal terminals[]) {
  MslCoreResult result;
  if (batch == NULL || inputs == NULL || observations == NULL || terminals == NULL) {
    return MSL_INVALID_ARGUMENT;
  }
  translate_inputs(batch, inputs);
  result = msl_core_batch_step_matches(batch->runtime, batch->inputs, sizeof(batch->inputs[0]),
                                       step_mask, step_mask != NULL ? sizeof(step_mask[0]) : 0);
  return result == MSL_CORE_OK ? msl_batch_observe(batch, observations, terminals)
                               : public_result(result);
}

MslResult msl_batch_step(MslBatch* batch, const MslInput inputs[], MslObservation observations[],
                         MslTerminal terminals[]) {
  return msl_batch_step_masked(batch, inputs, NULL, observations, terminals);
}

MslResult msl_batch_copy(MslBatch* destination, const MslBatch* source,
                         const uint32_t destination_indices[], const uint32_t source_indices[],
                         uint32_t count) {
  MslCoreResult result;
  uint32_t index;
  if (destination == NULL || source == NULL ||
      (count != 0 && (destination_indices == NULL || source_indices == NULL))) {
    return MSL_INVALID_ARGUMENT;
  }
  result = msl_core_batch_copy_matches(destination->runtime, source->runtime, destination_indices,
                                       source_indices, count);
  if (result != MSL_CORE_OK) {
    return public_result(result);
  }
  for (index = 0; index < count; ++index) {
    destination->viewpoint_players[destination_indices[index]] =
        source->viewpoint_players[source_indices[index]];
    destination->max_frames[destination_indices[index]] = source->max_frames[source_indices[index]];
  }
  return MSL_OK;
}

MslResult msl_batch_save_size(const MslBatch* batch, uint32_t env_index, size_t* required_size) {
  size_t payload_size;
  MslCoreResult result;
  if (batch == NULL || required_size == NULL) {
    return MSL_INVALID_ARGUMENT;
  }
  result = msl_core_batch_match_save_size(batch->runtime, env_index, &payload_size);
  if (result != MSL_CORE_OK) {
    return public_result(result);
  }
  if (payload_size > UINT32_MAX || payload_size > SIZE_MAX - sizeof(MslSaveHeader)) {
    return MSL_INVALID_STATE;
  }
  *required_size = sizeof(MslSaveHeader) + payload_size;
  return MSL_OK;
}

MslResult msl_batch_save(const MslBatch* batch, uint32_t env_index, void* buffer,
                         size_t buffer_size, size_t* written) {
  MslSaveHeader header;
  size_t required_size;
  size_t payload_written;
  MslResult public_status;
  MslCoreResult result;

  if (batch == NULL || buffer == NULL || written == NULL) {
    return MSL_INVALID_ARGUMENT;
  }
  public_status = msl_batch_save_size(batch, env_index, &required_size);
  if (public_status != MSL_OK) {
    return public_status;
  }
  if (buffer_size < required_size) {
    return MSL_INVALID_ARGUMENT;
  }
  memset(&header, 0, sizeof(header));
  header.magic = MSL_SAVE_MAGIC;
  header.version = MSL_SAVE_VERSION;
  header.payload_size = (uint32_t)(required_size - sizeof(header));
  header.max_frame = batch->max_frames[env_index];
  header.viewpoint_player = batch->viewpoint_players[env_index];
  memcpy(buffer, &header, sizeof(header));
  result = msl_core_batch_save_match(batch->runtime, env_index, (uint8_t*)buffer + sizeof(header),
                                     buffer_size - sizeof(header), &payload_written);
  if (result != MSL_CORE_OK) {
    return public_result(result);
  }
  *written = sizeof(header) + payload_written;
  return MSL_OK;
}

MslResult msl_batch_restore(MslBatch* batch, uint32_t env_index, const void* buffer,
                            size_t buffer_size) {
  MslSaveHeader header;
  MslCoreResult result;

  if (batch == NULL || buffer == NULL || env_index >= batch->size || buffer_size < sizeof(header)) {
    return MSL_INVALID_ARGUMENT;
  }
  memcpy(&header, buffer, sizeof(header));
  if (header.magic != MSL_SAVE_MAGIC || header.version != MSL_SAVE_VERSION ||
      header.payload_size != buffer_size - sizeof(header) ||
      header.viewpoint_player >= MSL_MAX_PLAYERS || header._reserved[0] != 0 ||
      header._reserved[1] != 0 || header._reserved[2] != 0) {
    return MSL_INCOMPATIBLE;
  }
  result = msl_core_batch_restore_match(
      batch->runtime, env_index, (const uint8_t*)buffer + sizeof(header), header.payload_size);
  if (result != MSL_CORE_OK) {
    return public_result(result);
  }
  batch->max_frames[env_index] = header.max_frame;
  batch->viewpoint_players[env_index] = header.viewpoint_player;
  return MSL_OK;
}
