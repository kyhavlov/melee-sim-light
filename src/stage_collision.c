#include "stage_collision.h"

void stage_collision_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // v1: Final Destination treated as a single flat ground segment at y=0.
  // Important: keep this bounded in X so we don't incorrectly "land" while offstage.
  const float ground_y = 0.0f;
  const float ground_y_epsilon = 0.001f;
  const float fd_floor_half_width = 85.7f;
  const float fd_side_segment_start_x = 75.0f;

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];
      const float vy = batch->state.speed_y_self[idx];
      const uint8_t in_floor_x = (uint8_t)(x >= -fd_floor_half_width && x <= fd_floor_half_width);
      const uint8_t near_floor_y = (uint8_t)(y <= (ground_y + ground_y_epsilon));
      const uint8_t moving_down_or_still = (uint8_t)(vy <= 0.0f);

      if (in_floor_x && near_floor_y && moving_down_or_still) {
        uint16_t ground_id = 1;
        if (x < -fd_side_segment_start_x) {
          ground_id = 0;
        } else if (x > fd_side_segment_start_x) {
          ground_id = 2;
        }

        batch->state.pos_y[idx] = ground_y;
        batch->state.on_ground[idx] = 1;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.ground_id[idx] = ground_id;
      } else {
        batch->state.on_ground[idx] = 0;
      }
    }
  }
}
