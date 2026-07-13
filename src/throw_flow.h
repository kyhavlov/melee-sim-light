#pragma once

#include "batch_internal.h"
#include "action_ids.h"

// Throw flow: consume movescript-driven throw flags and apply the throw hit + victim detachment.
//
// Decomp anchor:
// - Throw per-frame callback consumes `throw_flags_b3`/`throw_flags_b4` and triggers release:
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
//
// This module is intentionally small and data-driven:
// - release/flip timing comes from data/moves/{fox,falco}.json via move_tables APIs,
// - damage/KB/state entry is routed through combat_apply_throw_hit().
void throw_flow_update_anim_callback_pre_input(MslBatch* batch, int batch_index, int owner_p);

// Throw ground callback floor-loss continuation (`fn_800DD684`): release the linked victim and
// enter Fall for both fighters.
void throw_flow_ground_loss_release(MslBatch* batch, int batch_index, int owner_p);
void throw_flow_resume_attached_victim_after_hold(MslBatch* batch, int batch_index, int owner_p);

static inline uint8_t throw_flow_release_pending_for_victim(const MslBatch* batch, int bi,
                                                            int victim_p) {
  if (batch == NULL) {
    return 0u;
  }
  const int num_players = (int)batch->config.num_players;
  if (bi < 0 || victim_p < 0 || victim_p >= num_players) {
    return 0u;
  }
  for (int owner_p = 0; owner_p < num_players; owner_p++) {
    if (owner_p == victim_p) {
      continue;
    }
    const size_t oidx = msl_idx_player(bi, owner_p);
    if (batch->state.throw_pending_victim_port[oidx] == (uint8_t)victim_p &&
        batch->state.throw_pending_hit_idx[oidx] != 0xFFu) {
      // Shared ThrowF/B/Hi/Lw release owner:
      // - ftCo_800DD724 consumes set_throw_flags(0), detaches the victim, and immediately routes
      //   release placement / damage ownership from the thrower's Anim callback.
      // - While this simulator's deferred item-ordering bridge keeps that release hit pending, the
      //   same frame remains owned by throw release rather than generic Fall Phys/Coll/IASA.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t throw_flow_release_source_blocks_iasa(const MslBatch* batch, size_t idx,
                                                            uint16_t action_id) {
  if (batch == NULL || action_id != (uint16_t)MSL_ACT_FALL) {
    return 0u;
  }

  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const int victim_p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
  if (throw_flow_release_pending_for_victim(batch, bi, victim_p)) {
    return 1u;
  }

  // Teacher-forced/reseed fallback:
  // - A one-step seed can expose the simulator's detached Fall placeholder without carrying the
  //   internal same-frame pending latch.
  // - The source action is still the grabbed/thrown victim state, whose IASA callbacks are empty.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{
  //   ftCo_ThrownF_IASA,ftCo_ThrownB_IASA,ftCo_ThrownHi_IASA,ftCo_ThrownLw_IASA}
  return msl_action_is_grabbed_victim(batch->state.prev_action_id[idx]) ? 1u : 0u;
}

// Compatibility cleanup pass for stale teacher-forced pending latches. Runtime throw release damage
// is now applied by throw_flow_update_anim_callback_pre_input() in the source Anim callback phase.
void throw_flow_update_post_items(MslBatch* batch);
