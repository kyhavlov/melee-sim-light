#include "physics.h"

#include "char_params.h"

void physics_integrate(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      // Record pre-integration position for collision tests.
      // Ordering contract: stage_collision_apply() uses prev_pos_* captured here to perform
      // real "crossing" checks (pre vs post integration) without inferring prior position.
      batch->state.prev_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.prev_pos_y[idx] = batch->state.pos_y[idx];

      const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
      const float vx =
          on_ground ? batch->state.speed_ground_x_self[idx] : batch->state.speed_air_x_self[idx];
      const float vy = batch->state.speed_y_self[idx];

      batch->state.pos_x[idx] += vx;
      batch->state.pos_y[idx] += vy;

      if (!on_ground) {
        const MslCharPhysicsParams phys = msl_char_physics_params(batch->state.char_id[idx]);
        float next_vy = vy - phys.grav;
        if (next_vy < -phys.terminal_vel) {
          next_vy = -phys.terminal_vel;
        }
        batch->state.speed_y_self[idx] = next_vy;
      }
    }
  }
}
