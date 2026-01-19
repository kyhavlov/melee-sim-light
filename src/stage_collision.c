#include "stage_collision.h"

#include <float.h>

void stage_collision_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Final Destination grounding based on extracted stage collision segments.
  //
  // Source: `data/stages/final_destination.json` (ISO-derived).
  // We currently only support the main floor at y=0 (non-platform "floor" segments):
  // - left:  segment i=0  x ∈ [-68.4, -60.0]  (mapped to ground_id=0)
  // - mid:   segment i=1  x ∈ [-60.0,  60.0]  (mapped to ground_id=1)
  // - right: segment i=5  x ∈ [ 60.0,  68.4]  (mapped to ground_id=2)
  const float ground_y = 0.0f;
  const float fd_x0_left = -68.4f;
  const float fd_x1_left = -60.0f;
  const float fd_x0_mid = -60.0f;
  const float fd_x1_mid = 60.0f;
  const float fd_x0_right = 60.0f;
  const float fd_x1_right = 68.4f;
  // Numerical tolerance: allow tiny positive y to still count as on-ground for the y=0 floor.
  const float ground_y_epsilon = 1024.0f * FLT_EPSILON;

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    // Slippi stage id for Final Destination.
    if (batch->state.stage_id[bi] != 32) {
      for (int p = 0; p < num_players; p++) {
        const size_t idx = msl_idx_player(bi, p);
        batch->state.on_ground[idx] = 0;
      }
      continue;
    }

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];
      const float vy = batch->state.speed_y_self[idx];
      const uint8_t near_floor_y = (uint8_t)(y <= (ground_y + ground_y_epsilon));
      const uint8_t moving_down_or_still = (uint8_t)(vy <= 0.0f);

      uint16_t ground_id = 0;
      uint8_t in_floor_x = 0;
      if (x >= fd_x0_left && x <= fd_x1_left) {
        in_floor_x = 1;
        ground_id = 0;
      } else if (x >= fd_x0_mid && x <= fd_x1_mid) {
        in_floor_x = 1;
        ground_id = 1;
      } else if (x >= fd_x0_right && x <= fd_x1_right) {
        in_floor_x = 1;
        ground_id = 2;
      }

      if (in_floor_x && near_floor_y && moving_down_or_still) {
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
