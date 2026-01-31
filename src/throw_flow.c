#include "throw_flow.h"

#include "action_ids.h"
#include "anim_timebase.h"
#include "combat.h"
#include "move_tables.h"

static inline uint8_t is_thrower_action(uint16_t a) {
  switch (a) {
    case (uint16_t)MSL_ACT_THROW_F:
    case (uint16_t)MSL_ACT_THROW_B:
    case (uint16_t)MSL_ACT_THROW_HI:
    case (uint16_t)MSL_ACT_THROW_LW:
      return 1u;
    default:
      return 0u;
  }
}

static inline void enter_fall_release(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: generic fall entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  batch->state.on_ground[idx] = 0;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

void throw_flow_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  // Ordering note:
  // - `anim_timebase_update_pre_input()` runs before action_update() in step_one_frame().
  // - In decomp, movescript opcodes (e.g. set_throw_flags / set_throw_hitbox) are processed during
  //   the animation timebase advancement phase (ftAnim_8006EBA4), and the per-motion-state Anim
  //   callback (ftCo_*_Anim) consumes the resulting flags before Phys/Coll.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 and Fighter_procUpdate
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_Throw*_Anim
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int owner_p = 0; owner_p < num_players; owner_p++) {
      const size_t oidx = msl_idx_player(bi, owner_p);
      if (batch->state.stocks[oidx] == 0) {
        continue;
      }

      const uint16_t owner_act = batch->state.action_id[oidx];
      if (!is_thrower_action(owner_act)) {
        continue;
      }

      // Decomp: hitlag freezes Anim callback execution (Fighter_procUpdate does not run motion state
      // Anim/Phys/Coll callbacks while fp->x2219_b5 is set).
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      //
      // Without this guard, single-frame set_throw_flags events (notably facing flip) would be
      // re-applied every frame while anim_frame_f32 is frozen in hitlag.
      if (batch->state.hitlag[oidx] != 0) {
        continue;
      }

      const uint8_t owner_char = batch->state.char_id[oidx];
      const float owner_af = batch->state.anim_frame_f32[oidx];
      const int32_t owner_prev_fp =
          batch->state.anim_frame_fp_q16_16[oidx] - batch->state.frame_speed_mul_fp_q16_16[oidx];
      const float owner_prev_af = msl_f32_from_q16_16(owner_prev_fp);

      // Facing flip: set_throw_flags(hit_idx=1) -> throw_flags_b4 -> facing_dir = -facing_dir.
      // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4 (case 1)
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724 (inline1)
      if (move_tables_throw_should_flip_facing(owner_char, owner_act, owner_prev_af, owner_af)) {
        batch->state.facing[oidx] = (uint8_t)!batch->state.facing[oidx];
      }

      uint8_t hit_idx = 0;
      if (!move_tables_throw_release_hit_idx(owner_char, owner_act, owner_af, &hit_idx)) {
        continue;
      }

      MslThrowHitboxParams p = {0};
      if (!move_tables_throw_hitbox_params(owner_char, owner_act, hit_idx, &p)) {
        continue;
      }

      // Find the grabbed victim owned by this thrower (decomp: fp->victim_gobj is a single pointer).
      // If a malformed seed contains multiple victims with the same grab_owner_port, choose the
      // lowest port deterministically and process only that one.
      for (int victim_p = 0; victim_p < num_players; victim_p++) {
        if (victim_p == owner_p) {
          continue;
        }
        const size_t vidx = msl_idx_player(bi, victim_p);
        if (batch->state.stocks[vidx] == 0) {
          continue;
        }

        if (batch->state.grab_owner_port[vidx] != (uint8_t)owner_p) {
          continue;
        }
        const uint16_t victim_act = batch->state.action_id[vidx];
        if (!msl_action_is_grabbed_victim(victim_act)) {
          continue;
        }

        const uint8_t applied = combat_apply_throw_hit(batch, bi, owner_p, victim_p, &p);

        // Detach regardless of eligibility. If the throw hit is suppressed (e.g. defender is
        // invincible/intangible), ensure we still transition out of the grabbed/thrown victim loop
        // so the victim isn't left in a "no-physics, no-attachment" frozen state.
        batch->state.grab_owner_port[vidx] = 0xFFu;
        if (!applied) {
          // Defensive (suite-reachable?): in valid throw states, the victim should be in a Thrown*
          // action by the time set_throw_flags(hit_idx=0) fires. We still force Fall on any grabbed
          // victim if the throw hit is suppressed (invincible/intangible) so the victim can't be
          // left detached in a non-physics grabbed-victim loop.
          enter_fall_release(batch, vidx);
        }
        break;
      }
    }
  }
}
