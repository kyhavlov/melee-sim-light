#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
  enum { BATCH_SIZE = 2 };
  MslBatch* batch = NULL;
  MslMatchConfig configs[BATCH_SIZE];
  MslInput inputs[BATCH_SIZE] = {0};
  MslObservation observations[BATCH_SIZE];
  MslObservation expected;
  MslTerminal terminals[BATCH_SIZE];
  uint8_t reset_mask[BATCH_SIZE] = {0, 1};
  uint32_t destination = 1;
  uint32_t source = 0;
  void* save = NULL;
  size_t save_size = 0;
  size_t written = 0;
  int result = 1;
  const char* phase = "create";

  if (argc != 2) {
    fprintf(stderr, "usage: %s DATA_ROOT\n", argv[0]);
    return 2;
  }
  configs[0] = msl_match_config_default();
  configs[1] = msl_match_config_default();
  configs[0].viewpoint_player = 1;
  configs[1].stage = MSL_STAGE_BATTLEFIELD;
  configs[1].players[0].character = MSL_CHARACTER_MEWTWO;
  configs[1].players[1].character = MSL_CHARACTER_GAMEWATCH;
  configs[1].max_frame = 120;

  if (strcmp(msl_result_string(MSL_INVALID_ARGUMENT), "invalid argument") != 0 ||
      msl_batch_create(argv[1], BATCH_SIZE, &batch) != MSL_OK ||
      msl_batch_size(batch) != BATCH_SIZE ||
      msl_batch_step(batch, inputs, observations, terminals) != MSL_INVALID_STATE ||
      msl_batch_reset(batch, configs, NULL, observations) != MSL_OK) {
    goto done;
  }
  if (observations[0].frame_id != -123 || observations[0].stage_id != MSL_STAGE_FINAL_DESTINATION ||
      observations[0].viewpoint_player != 1 ||
      observations[0].slots[0].char_id != MSL_CHARACTER_FALCO ||
      observations[1].stage_id != MSL_STAGE_BATTLEFIELD ||
      observations[1].slots[0].char_id != MSL_CHARACTER_MEWTWO ||
      observations[1].slots[1].char_id != MSL_CHARACTER_GAMEWATCH) {
    goto done;
  }

  phase = "invalid config";
  configs[1].num_players = 1;
  if (msl_batch_reset(batch, configs, reset_mask, observations) != MSL_INVALID_ARGUMENT) {
    goto done;
  }
  phase = "three-player team config";
  configs[1].num_players = 3;
  configs[1].is_teams = 1;
  if (msl_batch_reset(batch, configs, reset_mask, observations) != MSL_OK ||
      msl_batch_observe(batch, observations, terminals) != MSL_OK ||
      observations[1].num_players != 3 || observations[1].is_teams != 1 ||
      terminals[1].alive_count != 3 || terminals[1].alive_team_count != 2) {
    goto done;
  }
  configs[1].num_players = 2;
  configs[1].is_teams = 0;
  if (msl_batch_reset(batch, configs, reset_mask, observations) != MSL_OK) {
    goto done;
  }

  phase = "save";
  if (msl_batch_save_size(batch, 0, &save_size) != MSL_OK || save_size == 0 ||
      (save = malloc(save_size)) == NULL ||
      msl_batch_save(batch, 0, save, save_size, &written) != MSL_OK || written != save_size) {
    goto done;
  }

  inputs[0].players[0].main_x = 80;
  phase = "step";
  {
    MslResult step_result = msl_batch_step(batch, inputs, observations, terminals);
    if (step_result != MSL_OK || terminals[0].done != 0 || terminals[1].done != 0) {
      fprintf(stderr, "step=%s frames=%d,%d done=%u,%u max=%u,%u\n", msl_result_string(step_result),
              observations[0].frame_id, observations[1].frame_id, terminals[0].done,
              terminals[1].done, terminals[0].max_frame_reached, terminals[1].max_frame_reached);
      goto done;
    }
  }
  expected = observations[0];

  inputs[1].players[0].main_x = 80;
  phase = "restore and copy";
  if (msl_batch_restore(batch, 1, save, save_size) != MSL_OK ||
      msl_batch_observe(batch, observations, terminals) != MSL_OK ||
      observations[1].frame_id != -123 || observations[1].viewpoint_player != 1 ||
      msl_batch_step(batch, inputs, observations, terminals) != MSL_OK ||
      memcmp(&observations[1], &expected, sizeof(expected)) != 0 ||
      msl_batch_copy(batch, batch, &destination, &source, 1) != MSL_OK ||
      msl_batch_observe(batch, observations, terminals) != MSL_OK ||
      memcmp(&observations[1], &observations[0], sizeof(observations[0])) != 0) {
    goto done;
  }

  phase = "shared game data";
  {
    // A second batch reuses this process's game data rather than loading its
    // own copy, and steps the same match to the same observation. Acquiring
    // the data explicitly (as a fork template would) keeps it loaded across
    // batch destruction; a different root is rejected.
    MslBatch* second = NULL;
    MslObservation second_observations[1];
    MslTerminal second_terminals[1];
    MslInput second_inputs[1] = {0};
    second_inputs[0].players[0].main_x = 80;
    if (msl_game_data_references() != 1 ||
        msl_batch_create(argv[1], 1, &second) != MSL_OK ||
        msl_game_data_references() != 2 ||
        msl_batch_reset(second, configs, NULL, second_observations) != MSL_OK ||
        msl_batch_step(second, second_inputs, second_observations, second_terminals) != MSL_OK ||
        memcmp(&second_observations[0], &expected, sizeof(expected)) != 0 ||
        msl_game_data_acquire("/nonexistent/other-root") != MSL_INVALID_STATE ||
        msl_game_data_acquire(argv[1]) != MSL_OK || msl_game_data_references() != 3) {
      msl_batch_destroy(second);
      goto done;
    }
    msl_batch_destroy(second);
    if (msl_game_data_references() != 2) {
      goto done;
    }
    msl_game_data_release();
    if (msl_game_data_references() != 1) {
      goto done;
    }
  }

  result = 0;

done:
  free(save);
  msl_batch_destroy(batch);
  if (result == 0 && msl_game_data_references() != 0) {
    result = 1;
    phase = "game data release";
  }
  if (result == 0) {
    puts("public C API smoke passed");
  } else {
    fprintf(stderr, "public C API smoke failed during %s\n", phase);
  }
  return result;
}
