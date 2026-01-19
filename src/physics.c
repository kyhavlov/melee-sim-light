#include "physics.h"

void physics_integrate(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // v1: simple constant gravity, applied only while airborne.
  // Note: Melee gravity is character-specific; we keep this deterministic skeleton minimal for now.
  const float gravity = 0.20f;

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
      const float vx = on_ground ? batch->state.speed_ground_x_self[idx] : batch->state.speed_air_x_self[idx];
      const float vy = batch->state.speed_y_self[idx];

      batch->state.pos_x[idx] += vx;
      batch->state.pos_y[idx] += vy;

      if (!on_ground) {
        batch->state.speed_y_self[idx] = vy - gravity;
      }
    }
  }
}
