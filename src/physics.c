#include "physics.h"

#include "action_ids.h"
#include "char_params.h"
#include "common_params.h"

// Input axes in MslStateSoA are Melee-legalized via ucf_clamp_stick_i8:
// ucf.h: clamp_stickMax = 80 (HSD_PadClampCheck3).
enum { MSL_STICK_MAX_I8 = 80 };

static inline float msl_absf(float x) { return x < 0.0f ? -x : x; }

static inline float stick_i8_to_unit(int8_t v) { return (float)v / (float)MSL_STICK_MAX_I8; }

static inline float apply_deadzone(float v, float dz) {
  if (msl_absf(v) < dz) {
    return 0.0f;
  }
  return v;
}

static inline uint8_t ftCommon_CheckFallFast(const MslCommonParams* c, float stick_y, float vy,
                                             uint8_t* io_fall_fast,
                                             uint8_t* io_x671_timer_lstick_tilt_y) {
  // refs/melee/src/melee/ft/ftcommon.c:505-520 (ftCommon_CheckFallFast)
  if (c == NULL || io_fall_fast == NULL || io_x671_timer_lstick_tilt_y == NULL) {
    return 0;
  }
  if (*io_fall_fast) {
    return 0;
  }
  if (!(vy < 0.0f)) {
    return 0;
  }
  if (!(stick_y <= -c->fastfall_stick_threshold)) {
    return 0;
  }
  if (!(*io_x671_timer_lstick_tilt_y < c->fastfall_tilt_max_frames)) {
    return 0;
  }

  *io_fall_fast = 1;
  *io_x671_timer_lstick_tilt_y = 0xFEu;
  return 1;
}

void physics_integrate(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
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
      const uint16_t action_id = batch->state.action_id[idx];
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

        const uint8_t allow_fastfall = msl_action_allows_fastfall(action_id);

        // Fastfall latch (ftCommon_CheckFallFast) is used by many aerial action states via a common
        // helper (`ft_80084DB0`), but it does not run for every airborne motion state (e.g. many
        // knockback/hitstun physics paths).
        //
        // Decomp refs:
        // - Check: refs/melee/src/melee/ft/ftcommon.c:505-520 (ftCommon_CheckFallFast)
        // - Common call ordering: refs/melee/src/melee/ft/ft_081B.c:1347-1359 (ft_80084DB0)
        if (allow_fastfall) {
          const float stick_y = apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]),
                                               c->lstick_deadzone_y);
          (void)ftCommon_CheckFallFast(c, stick_y, vy, &batch->state.fall_fast[idx],
                                       &batch->state.tilt_timer_y[idx]);
        }

        // Air gravity / terminal velocity / fastfall.
        //
        // Decomp refs:
        // - Gravity/terminal: refs/melee/src/melee/ft/ftcommon.c::ftCommon_Fall
        // - Fastfall: refs/melee/src/melee/ft/ftcommon.c::ftCommon_FallFast (called via ft_80084DB0)
        float next_vy = vy;
        if (allow_fastfall && batch->state.fall_fast[idx]) {
          // refs/melee/src/melee/ft/ftcommon.c:488-494 (ftCommon_FallFast)
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
