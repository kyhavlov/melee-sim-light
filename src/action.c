#include "action.h"

#include <stdint.h>

void action_update(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      // Hitlag freezes action/animation advancement:
      // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 gates `ftAnim_8006EBA4(gobj)` under
      //   `if (!fp->x2219_b5)`, and Slippi records AS frame from 0x894.
      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      const int16_t af = batch->state.action_frame[idx];
      if (af < INT16_MAX) {
        batch->state.action_frame[idx] = (int16_t)(af + 1);
      }
    }
  }
}
