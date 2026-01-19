#include "stage_collision.h"

void stage_collision_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // v1: Final Destination treated as a single infinite ground plane at y=0.
  const float ground_y = 0.0f;
  const uint16_t ground_id_grounded = 0;
  const uint16_t ground_id_airborne = 65535;

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      const float y = batch->state.pos_y[idx];
      if (y <= ground_y) {
        batch->state.pos_y[idx] = ground_y;
        batch->state.on_ground[idx] = 1;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.ground_id[idx] = ground_id_grounded;
      } else {
        batch->state.on_ground[idx] = 0;
        batch->state.ground_id[idx] = ground_id_airborne;
      }
    }
  }
}
