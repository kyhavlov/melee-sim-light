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
      batch->state.prev_on_ground[idx] = batch->state.on_ground[idx] ? 1 : 0;

      // Hitlag freezes motion/physics advancement:
      // - refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate runs its main integration block only
      //   under `if (!fp->x2219_b5)`.
      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
      const float vx =
          on_ground ? batch->state.speed_ground_x_self[idx] : batch->state.speed_air_x_self[idx];
      const float vy = batch->state.speed_y_self[idx];

      batch->state.pos_x[idx] += vx;
      batch->state.pos_y[idx] += vy;

      if (!on_ground) {
        const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
        if (phys == NULL) {
          continue;
        }

        // Air gravity / terminal velocity / fastfall.
        //
        // Decomp refs:
        // - Gravity/terminal: refs/melee/src/melee/ft/ftcommon.c::ftCommon_Fall
        // - Fastfall: refs/melee/src/melee/ft/ftcommon.c::ftCommon_FallFast (called via ft_80084DB0)
        float next_vy = vy;
        if (next_vy <= -phys->fast_fall_velocity) {
          next_vy = -phys->fast_fall_velocity;
        } else {
          next_vy -= phys->grav;
          if (next_vy < -phys->terminal_vel) {
            next_vy = -phys->terminal_vel;
          }
        }
        batch->state.speed_y_self[idx] = next_vy;
      }
    }
  }
}
