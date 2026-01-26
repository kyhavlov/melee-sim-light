#include "physics.h"

#include "action_ids.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"

static inline uint8_t physics_is_match_flow_airborne(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_DEAD_DOWN:
    case MSL_ACT_DEAD_LEFT:
    case MSL_ACT_DEAD_RIGHT:
    case MSL_ACT_DEAD_UP_STAR:
    case MSL_ACT_REBIRTH:
    case MSL_ACT_REBIRTH_WAIT:
    case MSL_ACT_ENTRY:
    case MSL_ACT_ENTRY_START:
    case MSL_ACT_ENTRY_END:
    // Cliff / ledge actions are updated via dedicated callbacks in-engine and should not receive
    // generic gravity/fastfall updates in this simplified core.
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return 1;
    default:
      return 0;
  }
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

      // Ledge grab cooldown timer (x2064_ledgeCooldown) decrements only when not in hitlag.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      if (batch->state.ledge_cooldown[idx] != 0) {
        batch->state.ledge_cooldown[idx]--;
      }

      const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
      const uint16_t action_id = batch->state.action_id[idx];
      const float vx_self =
          on_ground ? batch->state.speed_ground_x_self[idx] : batch->state.speed_air_x_self[idx];
      const float vy_self = batch->state.speed_y_self[idx];

      // Knockback velocity contributes to position integration in addition to self velocity.
      //
      // Decomp: position integrates both self velocity and knockback velocity.
      // - refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate:
      //   `p_kb_vel = &fp->x8c_kb_vel;`
      //   `PSVECAdd(&fp->cur_pos, &selfVel, &fp->cur_pos); fp->cur_pos.x += p_kb_vel->x; fp->cur_pos.y += p_kb_vel->y;`
      //
      // Seed mapping: our `speed_{x,y}_attack` are Slippi post-frame `velocities.knockback_{x,y}`
      // (i.e. the decomp `fp->x8c_kb_vel.{x,y}` knockback velocity term), not a "self velocity".
      // - tools/slippi/make_dataset_from_slp.py (velocities.knockback_{x,y} → speed_{x,y}_attack)
      //
      // We include `speed_*_attack` in position integration (because it affects where the character is this frame),
      // but we exclude it from gravity/fastfall updates (which operate on `self_vel` only; knockback has its own
      // separate decay/physics paths in-engine, and is currently teacher-forced from the seed).
      const float vx = vx_self + batch->state.speed_x_attack[idx];
      const float vy_integrate = vy_self + batch->state.speed_y_attack[idx];

      // Position integration happens before gravity/fastfall updates in this simplified core.
      batch->state.pos_x[idx] += vx;
      batch->state.pos_y[idx] += vy_integrate;

      if (on_ground) {
        continue;
      }

      // Match-flow action states (KO/death/respawn/entry) are suite-modeled as non-physical:
      // no gravity/fastfall, no hidden decay. Position updates (if any) are driven explicitly by
      // match_flow.c and/or the seeded self-vel.
      if (physics_is_match_flow_airborne(action_id)) {
        continue;
      }

      // CliffJump2 is a special case: its physics callback skips the common fall helper on the
      // first frame (no gravity/fastfall update that frame), then uses ft_80084DB0 afterward.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Phys
      if (action_id == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2 && batch->state.action_frame[idx] <= 0) {
        continue;
      }

      // EscapeAir is a self-velocity-controlled state with its own decay; do not apply gravity or
      // fastfall here unless we later model cmd_skip_decay.
      //
      // Decomp: ftCo_EscapeAir_Phys scales `self_vel` by `escapeair_decay` when cmd_skip_decay is false,
      // and otherwise calls the common fall helper (`ft_80084DB0`).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Phys
      if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
        batch->state.speed_air_x_self[idx] *= c->escapeair_decay;
        batch->state.speed_y_self[idx] *= c->escapeair_decay;
        continue;
      }

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
        const float stick_y =
            apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
        (void)ftCommon_CheckFallFast(c, stick_y, vy_self, &batch->state.fall_fast[idx],
                                     &batch->state.tilt_timer_y[idx]);
      }

      // Air gravity / terminal velocity / fastfall.
      //
      // Decomp refs:
      // - Gravity/terminal: refs/melee/src/melee/ft/ftcommon.c::ftCommon_Fall
      // - Fastfall: refs/melee/src/melee/ft/ftcommon.c::ftCommon_FallFast (called via ft_80084DB0)
      float next_vy = vy_self;
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
