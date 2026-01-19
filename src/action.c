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

      // Hook: action frame advancement may be gated by hitlag/other timers later.
      const int16_t af = batch->state.action_frame[idx];
      if (af < INT16_MAX) {
        batch->state.action_frame[idx] = (int16_t)(af + 1);
      }
    }
  }
}
