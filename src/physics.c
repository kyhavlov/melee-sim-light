#include "physics.h"

#include "action_ids.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"

static inline uint8_t physics_action_use_pre_integration_common_air_gravity(uint16_t action_id) {
  // Decomp: many common airborne action states call `ft_80084DB0` from their phys callbacks, which
  // runs `ftCommon_CheckFallFast` + `ftCommon_Fall/FallFast` (mutating `self_vel.y`) before
  // `Fighter_procUpdate` integrates `cur_pos`.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  //
  // However, in the v1 teacher-forced reseed loop we currently treat Damage*/hitstun-y states as
  // mostly seed-driven. Applying the common fall helper for DamageFall shifts `pos_y` by ~grav on
  // some frames and can cause match-flow blastzone false positives (stocks decremented) vs the
  // replay reference.
  //
  // Repro (suite): `AttachedGoodNaturedGuanaco.msl` record=2959 p=0 (DamageFall) crosses FD blast
  // bottom and triggers `MSL_ACT_DEAD_DOWN` when we apply common gravity pre-integration.
  //
  // Until DamageFall's full physics path is modeled (including its interaction with hitstun/KB),
  // keep the ordering fix for locomotion/attackair states but exclude DamageFall here.
  if (!msl_action_allows_fastfall(action_id)) {
    return 0;
  }
  return (uint8_t)(action_id != (uint16_t)MSL_ACT_DAMAGE_FALL);
}

static inline uint8_t physics_action_use_post_integration_common_air_gravity(uint16_t action_id) {
  // Temporary v1 compatibility: update `speed_y_self` for next frame without affecting current
  // frame displacement (see note above).
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_DAMAGE_FALL);
}

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
      const float vy_self_pre = batch->state.speed_y_self[idx];

      // ----------------------------
      // Air-only self-velocity update
      // ----------------------------
      //
      // Decomp ordering:
      // - Motion-state phys callbacks run inside `Fighter_procUpdate` before position integration.
      //   refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      // - Many airborne states call the common fall helper `ft_80084DB0`, which runs:
      //   (1) ftCommon_CheckFallFast, (2) ftCommon_Fall/FallFast (mutates fp->self_vel.y),
      //   (3) ftCommon_8007D268 (air drift accel via fp->x74_anim_vel.x).
      //   refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
      //   refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
      //   refs/melee/src/melee/ft/ftcommon.c::ftCommon_Fall / ftCommon_FallFast
      //
      // In GALE01, `fp->cur_pos` is then integrated using the updated self velocity in the same
      // proc. We therefore apply gravity/fastfall (and simplified EscapeAir decay) before
      // integrating `pos_*` so our one-step outputs are aligned with the in-engine ordering.
      if (!on_ground) {
        // Match-flow and cliff actions are treated as non-physical in this simplified core.
        if (!physics_is_match_flow_airborne(action_id)) {
          // CliffJump2 special-case: its phys callback skips the common fall helper on the first
          // frame. refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Phys
          if (!(action_id == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2 &&
                batch->state.action_frame[idx] <= 0)) {
            if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
              // Decomp: EscapeAir_Phys scales `self_vel` by `escapeair_decay` when cmd_skip_decay is
              // false; otherwise it calls `ft_80084DB0`.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Phys
              //
              // This sim currently does not model cmd_skip_decay, and always uses the decay path.
              batch->state.speed_air_x_self[idx] *= c->escapeair_decay;
              batch->state.speed_y_self[idx] *= c->escapeair_decay;
            } else if (physics_action_use_pre_integration_common_air_gravity(action_id)) {
              const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
              if (phys != NULL) {
                const uint8_t allow_fastfall = msl_action_allows_fastfall(action_id);

                // Fastfall latch (ftCommon_CheckFallFast) uses the pre-gravity `self_vel.y`.
                // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
                if (allow_fastfall) {
                  const float stick_y = apply_deadzone(
                      stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
                  (void)ftCommon_CheckFallFast(c, stick_y, vy_self_pre, &batch->state.fall_fast[idx],
                                               &batch->state.tilt_timer_y[idx]);
                }

                // Air gravity / terminal velocity / fastfall update.
                //
                // Decomp refs:
                // - Gravity/terminal: refs/melee/src/melee/ft/ftcommon.c::ftCommon_Fall
                // - Fastfall: refs/melee/src/melee/ft/ftcommon.c::ftCommon_FallFast (called via ft_80084DB0)
                float next_vy = vy_self_pre;
                if (allow_fastfall && batch->state.fall_fast[idx]) {
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
      }

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
      const float vx_self =
          on_ground ? batch->state.speed_ground_x_self[idx] : batch->state.speed_air_x_self[idx];
      const float vy_self = batch->state.speed_y_self[idx];
      const float vx = vx_self + batch->state.speed_x_attack[idx];
      const float vy_integrate = vy_self + batch->state.speed_y_attack[idx];

      // Position integration uses the (possibly-updated) self velocity plus the separate knockback
      // velocity term, matching GALE01 `Fighter_procUpdate` integration shape.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      batch->state.pos_x[idx] += vx;
      batch->state.pos_y[idx] += vy_integrate;

      // Post-integration gravity update for states we intentionally keep "seed-driven" for current
      // frame displacement (notably DamageFall; see helper docs above).
      if (!on_ground && !physics_is_match_flow_airborne(action_id) &&
          physics_action_use_post_integration_common_air_gravity(action_id)) {
        const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
        if (phys != NULL) {
          const uint8_t allow_fastfall = msl_action_allows_fastfall(action_id);
          if (allow_fastfall) {
            const float stick_y = apply_deadzone(
                stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
            (void)ftCommon_CheckFallFast(c, stick_y, vy_self_pre, &batch->state.fall_fast[idx],
                                         &batch->state.tilt_timer_y[idx]);
          }

          float next_vy = vy_self_pre;
          if (allow_fastfall && batch->state.fall_fast[idx]) {
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
}
